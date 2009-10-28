#ifndef __MORDOR_FIBER_H__
#define __MORDOR_FIBER_H__
// Copyright (c) 2009 - Decho Corp.

#ifdef WIN32
#else
#include <stddef.h>
#endif

#include <list>

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "exception.h"
#include "thread_local_storage.h"
#include "version.h"

// Fiber impl selection

#ifdef X86_64
#   ifdef WINDOWS
#       define NATIVE_WINDOWS_FIBERS
#   elif defined(LINUX)
#       define UCONTEXT_FIBERS
#   elif defined(OSX)
#       define SETJMP_FIBERS
#   elif defined(POSIX)
#       define ASM_X86_64_POSIX_FIBERS
#   endif
#elif defined(X86)
#   ifdef WINDOWS
#       define NATIVE_WINDOWS_FIBERS
#   elif defined(OSX)
#       define SETJMP_FIBERS
#   elif defined(POSIX)
#       define ASM_X86_POSIX_FIBERS
#   endif
#else
#   error Platform not supported
#endif

#ifdef UCONTEXT_FIBERS
#include <ucontext.h>
#endif
#ifdef SETJMP_FIBERS
#include <setjmp.h>
#endif

namespace Mordor {

/// Cooperative Thread
class Fiber : public boost::enable_shared_from_this<Fiber>
{
public:
    typedef boost::shared_ptr<Fiber> ptr;
    typedef boost::weak_ptr<Fiber> weak_ptr;

    /// The current execution state of a Fiber
    enum State
    {
        /// Initialized, but not run
        INIT,
        /// Currently suspended
        HOLD,
        /// Currently executing
        EXEC,
        /// Terminated because of an exception
        EXCEPT,
        /// Terminated
        TERM
    };

public:
    /// @brief Create a Fiber for the currently executing thread
    /// @pre No other Fiber object represents the currently executing thread
    /// @post state() == EXEC
    Fiber();
    /// @brief Create a new Fiber
    /// @param dg The initial function
    /// @param stacksize An explicit size for the stack.  This is initial size in virtual
    /// memory; physical/paging memory is not allocated until the actual pages
    /// are touched by the Fiber executing
    /// @post state() == INIT
    Fiber(boost::function<void ()> dg, size_t stacksize = 0);
    ~Fiber();

    /// @brief Reset a Fiber to be used again
    /// @pre state() == INIT || state() == TERM || state() == EXCEPT
    /// @post state() == INIT
    void reset();
    /// @brief Reset a Fiber to be used again, with a different initial function
    /// @param dg The new initial function
    /// @pre state() == INIT || state() == TERM || state() == EXCEPT
    /// @post state() == INIT
    void reset(boost::function<void ()> dg);

    /// @return The currently executing Fiber
    static ptr getThis();

    /// Call a Fiber

    /// The Fiber is executed as a "child" Fiber of the currently executing
    /// Fiber.  The currently executing Fiber is left in the EXEC state,
    /// and this Fiber also transitions to the EXEC state by either calling
    /// the initial function, or returning from yield() or yieldTo().
    ///
    /// call() does not return until the Fiber calls yield(), returns,
    /// or throws an exception.
    /// @pre Fiber::getThis() != NULL
    /// @pre state() == INIT || state() == HOLD
    void call();

    /// Inject an exception into a Fiber

    /// The Fiber is executed, but instead of returning from yield() or
    /// yieldTo(), exception is rethrown in the Fiber
    /// @param exception The exception to be rethrown in the Fiber
    /// @pre Fiber::getThis() != NULL
    /// @pre state() == INIT || state() == HOLD
    void inject(boost::exception_ptr exception);

    /// Yield execution to a specific Fiber

    /// The Fiber is executed by replacing the currently executing Fiber.
    /// The currently executing Fiber transitions to the HOLD state, and this
    /// Fiber transitions to the EXEC state, by either calling the initial
    /// function, or returning from yield() or yieldTo().
    ///
    /// yieldTo() does not return until another Fiber calls yieldTo() on the
    /// currently executing Fiber, or yieldToCallerOnTerminate is true and
    /// this fiber returns or throws an exception
    /// @param yieldToCallerOnTerminate Whether to keep a weak reference back
    /// to the currently executing Fiber in order to yield back to it when this
    /// Fiber terminates
    /// @return The Fiber that yielded back
    /// (not necessarily the Fiber that was yielded to)
    /// @pre Fiber::getThis() != NULL
    /// @pre state() == INIT || state() == HOLD
    Fiber::ptr yieldTo(bool yieldToCallerOnTerminate = true);
    
    /// Yield to the calling Fiber

    /// yield() returns when the Fiber has been called or yielded to again
    /// @pre Fiber::getThis() != NULL
    /// @pre This Fiber was executed by call()
    static void yield();

    /// The current execution state of the Fiber
    State state();

private:
    void call(bool destructor);
    Fiber::ptr yieldTo(bool yieldToCallerOnTerminate, State targetState);
    static void setThis(Fiber *f);
    static void entryPoint();
    static void exitPoint(Fiber::ptr &cur, Fiber *curp, State targetState);

    void allocStack();
    void freeStack();
    void initStack();

private:
    boost::function<void ()> m_dg;
    void *m_stack, *m_sp;
    size_t m_stacksize;
#ifdef UCONTEXT_FIBERS
    ucontext_t m_ctx;
#ifdef OSX
    char m_mctx[sizeof(*(mcontext_t)0)];
#endif
#elif defined(SETJMP_FIBERS)
    jmp_buf m_env;
#endif
#if defined(LINUX) || defined(OSX)
    int m_valgrindStackId;
#endif
    State m_state, m_yielderNextState;
    ptr m_outer, m_yielder;
    weak_ptr m_terminateOuter;
    boost::exception_ptr m_exception;

    static ThreadLocalStorage<Fiber> t_fiber;
};

}

#endif // __FIBER_H__