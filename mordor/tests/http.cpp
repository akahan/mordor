// Copyright (c) 2009 - Mozy, Inc.

#include "mordor/pch.h"

#include <boost/bind.hpp>

#include "mordor/http/broker.h"
#include "mordor/http/client.h"
#include "mordor/http/parser.h"
#include "mordor/http/server.h"
#include "mordor/scheduler.h"
#include "mordor/streams/cat.h"
#include "mordor/streams/duplex.h"
#include "mordor/streams/limited.h"
#include "mordor/streams/memory.h"
#include "mordor/streams/notify.h"
#include "mordor/streams/null.h"
#include "mordor/streams/random.h"
#include "mordor/streams/test.h"
#include "mordor/streams/transfer.h"
#include "mordor/test/test.h"
#include "mordor/util.h"

using namespace Mordor;
using namespace Mordor::HTTP;
using namespace Mordor::Test;

// Simplest success case
MORDOR_UNITTEST(HTTP, simpleRequest)
{
    Request request;
    RequestParser parser(request);

    parser.run("GET / HTTP/1.0\r\n\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
}

MORDOR_UNITTEST(HTTP, requestWithQuery)
{
    Request request;
    RequestParser parser(request);

    parser.run("POST /ab/d/e/wasdkfe/?ohai=1 HTTP/1.1\r\n\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, POST);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/ab/d/e/wasdkfe/?ohai=1"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 1));
}

MORDOR_UNITTEST(HTTP, emptyRequest)
{
    Request request;
    RequestParser parser(request);

    parser.run("");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
}

MORDOR_UNITTEST(HTTP, garbageRequest)
{
    Request request;
    RequestParser parser(request);

    parser.run("#*((@Nflk:J");
    MORDOR_TEST_ASSERT(parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
}

MORDOR_UNITTEST(HTTP, missingNewlineRequest)
{
    Request request;
    RequestParser parser(request);

    parser.run("GET / HTTP/1.0\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
    // Even though it's not complete, we should have parsed as much as was there
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
}

MORDOR_UNITTEST(HTTP, requestWithSimpleHeader)
{
    Request request;
    RequestParser parser(request);

    parser.run("GET / HTTP/1.0\r\n"
               "Connection: close\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.general.connection.size(), 1u);
    MORDOR_TEST_ASSERT(request.general.connection.find("close")
        != request.general.connection.end());
}

MORDOR_UNITTEST(HTTP, requestWithComplexHeader)
{
    Request request;
    RequestParser parser(request);

    parser.run("GET / HTTP/1.0\r\n"
               "Connection:\r\n"
               " keep-alive,  keep-alive\r\n"
               "\t, close\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.general.connection.size(), 2u);
    MORDOR_TEST_ASSERT(request.general.connection.find("close")
        != request.general.connection.end());
    MORDOR_TEST_ASSERT(request.general.connection.find("keep-alive")
        != request.general.connection.end());
}

MORDOR_UNITTEST(HTTP, ifMatchHeader)
{
    Request request;
    RequestParser parser(request);
    ETagSet::iterator it;
    std::ostringstream os;

    parser.run("GET / HTTP/1.0\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT(request.request.ifMatch.empty());
    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "\r\n");

    request = Request();
    parser.run("GET / HTTP/1.0\r\n"
               "If-Match: *\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.request.ifMatch.size(), 1u);
    it = request.request.ifMatch.begin();
    MORDOR_TEST_ASSERT(it->unspecified);
    MORDOR_TEST_ASSERT(!it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "");
    os.str("");
    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "If-Match: *\r\n"
        "\r\n");

    request = Request();
    parser.run("GET / HTTP/1.0\r\n"
               "If-Match: \"\", W/\"other\", \"something\"\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.request.ifMatch.size(), 3u);
    it = request.request.ifMatch.begin();
    MORDOR_TEST_ASSERT(!it->unspecified);
    MORDOR_TEST_ASSERT(!it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "");
    ++it;
    MORDOR_TEST_ASSERT(!it->unspecified);
    MORDOR_TEST_ASSERT(!it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "something");
    ++it;
    MORDOR_TEST_ASSERT(!it->unspecified);
    MORDOR_TEST_ASSERT(it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "other");
    os.str("");
    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "If-Match: \"\", \"something\", W/\"other\"\r\n"
        "\r\n");

    // * is only allowed once
    request = Request();
    parser.run("GET / HTTP/1.0\r\n"
               "If-Match: \"first\", \"second\", *\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.request.ifMatch.size(), 2u);
    it = request.request.ifMatch.begin();
    MORDOR_TEST_ASSERT(!it->unspecified);
    MORDOR_TEST_ASSERT(!it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "first");
    ++it;
    MORDOR_TEST_ASSERT(!it->unspecified);
    MORDOR_TEST_ASSERT(!it->weak);
    MORDOR_TEST_ASSERT_EQUAL(it->value, "second");
}

MORDOR_UNITTEST(HTTP, upgradeHeader)
{
    Request request;
    RequestParser parser(request);
    ProductList::iterator it;
    std::ostringstream os;

    parser.run("GET / HTTP/1.0\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT(request.general.upgrade.empty());
    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "\r\n");

    request = Request();
    parser.run("GET / HTTP/1.0\r\n"
               "Upgrade: HTTP/2.0, SHTTP/1.3, IRC/6.9, RTA/x11\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.general.upgrade.size(), 4u);
    it = request.general.upgrade.begin();
    MORDOR_TEST_ASSERT_EQUAL(it->product, "HTTP");
    MORDOR_TEST_ASSERT_EQUAL(it->version, "2.0");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->product, "SHTTP");
    MORDOR_TEST_ASSERT_EQUAL(it->version, "1.3");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->product, "IRC");
    MORDOR_TEST_ASSERT_EQUAL(it->version, "6.9");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->product, "RTA");
    MORDOR_TEST_ASSERT_EQUAL(it->version, "x11");
    ++it;
    os.str("");
    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "Upgrade: HTTP/2.0, SHTTP/1.3, IRC/6.9, RTA/x11\r\n"
        "\r\n");

    request = Request();
    parser.run("GET / HTTP/1.0\r\n"
               "Upgrade: HTTP/2.0, SHTTP/1.<3, IRC/6.9, RTA/x11\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(request.general.upgrade.size(), 1u);
    it = request.general.upgrade.begin();
    MORDOR_TEST_ASSERT_EQUAL(it->product, "HTTP");
    MORDOR_TEST_ASSERT_EQUAL(it->version, "2.0");
}

MORDOR_UNITTEST(HTTP, serverHeader)
{
    Response response;
    ResponseParser parser(response);
    ProductAndCommentList::iterator it;
    std::ostringstream os;

    parser.run("HTTP/1.0 200 OK\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT(response.general.upgrade.empty());
    os << response;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "HTTP/1.0 200 OK\r\n"
        "\r\n");

    response = Response();
    parser.run("HTTP/1.0 200 OK\r\n"
               "Server: Apache/2.2.3 (Debian) mod_fastcgi/2.4.2 mod_python/3.2.10 Python/2.4.4 PHP/4.4.4-8+etch6\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT_EQUAL(response.response.server.size(), 6u);
    it = response.response.server.begin();
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).product, "Apache");
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).version, "2.2.3");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(boost::get<std::string>(*it), "Debian");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).product, "mod_fastcgi");
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).version, "2.4.2");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).product, "mod_python");
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).version, "3.2.10");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).product, "Python");
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).version, "2.4.4");
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).product, "PHP");
    MORDOR_TEST_ASSERT_EQUAL(boost::get<Product>(*it).version, "4.4.4-8+etch6");
    ++it;
    os.str("");
    os << response;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "HTTP/1.0 200 OK\r\n"
        "Server: Apache/2.2.3 (Debian) mod_fastcgi/2.4.2 mod_python/3.2.10 Python/2.4.4 PHP/4.4.4-8+etch6\r\n"
        "\r\n");
}

MORDOR_UNITTEST(HTTP, teHeader)
{
    Request request;
    RequestParser parser(request);
    std::ostringstream os;

    parser.run("GET / HTTP/1.0\r\n"
               "TE: deflate, chunked;q=0, x-gzip;q=0.050\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.request.te.size(), 3u);
    AcceptListWithParameters::iterator it = request.request.te.begin();
    MORDOR_TEST_ASSERT_EQUAL(it->value, "deflate");
    MORDOR_TEST_ASSERT_EQUAL(it->qvalue, ~0u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->value, "chunked");
    MORDOR_TEST_ASSERT_EQUAL(it->qvalue, 0u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->value, "x-gzip");
    MORDOR_TEST_ASSERT_EQUAL(it->qvalue, 50u);

    os << request;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "GET / HTTP/1.0\r\n"
        "TE: deflate, chunked;q=0, x-gzip;q=0.05\r\n"
        "\r\n");
}

MORDOR_UNITTEST(HTTP, trailer)
{
    EntityHeaders trailer;
    TrailerParser parser(trailer);

    parser.run("Content-Type: text/plain\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(trailer.contentType.type, "text");
    MORDOR_TEST_ASSERT_EQUAL(trailer.contentType.subtype, "plain");
}

MORDOR_UNITTEST(HTTP, rangeHeader)
{
    Request request;
    RequestParser parser(request);

    parser.run("GET / HTTP/1.1\r\n"
               "Range: bytes=0-499, 500-999, -500, 9500-, 0-0,-1\r\n"
               " ,500-600\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.method, GET);
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.uri, URI("/"));
    MORDOR_TEST_ASSERT_EQUAL(request.requestLine.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(request.request.range.size(), 7u);
    RangeSet::const_iterator it = request.request.range.begin();
    MORDOR_TEST_ASSERT_EQUAL(it->first, 0u);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 499u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, 500u);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 999u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, ~0ull);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 500u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, 9500u);
    MORDOR_TEST_ASSERT_EQUAL(it->second, ~0ull);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, 0u);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 0u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, ~0ull);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 1u);
    ++it;
    MORDOR_TEST_ASSERT_EQUAL(it->first, 500u);
    MORDOR_TEST_ASSERT_EQUAL(it->second, 600u);
}

MORDOR_UNITTEST(HTTP, contentTypeHeader)
{
    Response response;
    ResponseParser parser(response);

    parser.run("HTTP/1.1 200 OK\r\n"
               "Content-Type: text/plain\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT_EQUAL(response.entity.contentType.type, "text");
    MORDOR_TEST_ASSERT_EQUAL(response.entity.contentType.subtype, "plain");
}

MORDOR_UNITTEST(HTTP, eTagHeader)
{
    Response response;
    ResponseParser parser(response);
    std::ostringstream os;

    parser.run("HTTP/1.1 200 OK\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT(response.response.eTag.unspecified);
    MORDOR_TEST_ASSERT(!response.response.eTag.weak);
    MORDOR_TEST_ASSERT_EQUAL(response.response.eTag.value, "");
    os << response;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "HTTP/1.1 200 OK\r\n"
        "\r\n");

    response = Response();
    parser.run("HTTP/1.1 200 OK\r\n"
               "ETag: \"\"\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT(!response.response.eTag.unspecified);
    MORDOR_TEST_ASSERT(!response.response.eTag.weak);
    MORDOR_TEST_ASSERT_EQUAL(response.response.eTag.value, "");
    os.str("");
    os << response;
    MORDOR_TEST_ASSERT_EQUAL(os.str(),
        "HTTP/1.1 200 OK\r\n"
        "ETag: \"\"\r\n"
        "\r\n");

    response = Response();
    parser.run("HTTP/1.1 200 OK\r\n"
               "ETag: W/\"\"\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT(!response.response.eTag.unspecified);
    MORDOR_TEST_ASSERT(response.response.eTag.weak);
    MORDOR_TEST_ASSERT_EQUAL(response.response.eTag.value, "");

    response = Response();
    parser.run("HTTP/1.1 200 OK\r\n"
               "ETag: \"sometag\"\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT(!response.response.eTag.unspecified);
    MORDOR_TEST_ASSERT(!response.response.eTag.weak);
    MORDOR_TEST_ASSERT_EQUAL(response.response.eTag.value, "sometag");

    response = Response();
    parser.run("HTTP/1.1 200 OK\r\n"
               "ETag: *\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");

    response = Response();
    parser.run("HTTP/1.1 200 OK\r\n"
               "ETag: token\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(parser.error());
    MORDOR_TEST_ASSERT(!parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
}

MORDOR_UNITTEST(HTTP, locationHeader)
{
    Response response;
    ResponseParser parser(response);

    parser.run("HTTP/1.1 307 OK\r\n"
               "Location: /partialObjects/"
        "49ZtbkNPlEEi8T+sQLb5mh9zm1DcyaaRoyHUOC9sEfaKIgLh+eKZNUrqR+j3Iybhx321iz"
        "y3J+Mw7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoXcN18+x4XedNLqc8KhnJtHLXndcKMJu5Cg"
        "xp2BI9NXDDDuBmYiVVxms/Soc0PG4RAcxQcurGFehSY0Wf0fG5eWquA0b0hozVjE4xxyAF"
        "TkSU39Hl3XcsUUMO4GZiJVXGaz9KhzQ8bhEBzFBy6sYV6F9718Fox0OiJ3PqBvo2gr352W"
        "vZBqmEeUV1n0CkcClc0w7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoWapmDUfha0WO9SjTUn4F"
        "Jeht8Gjdy6mYpDqvUbB+3OoDDuBmYiVVxms/Soc0PG4RAcxQcurGFehcefjKkVeAR2HShU"
        "2UpBh5g/89ZP9czSJ8qKSKCPGyHWMO4GZiJVXGaz9KhzQ8bhEBzFBy6sYV6FAig0fJADqV"
        "eInu5RU/pgEXJlZ1MBce/F+rv7MI3g5jgw7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoW4GIxe"
        "C1lnhkTtrAv3jhk17r3ZwL8Fq7CvpUHeAQl/JTDuBmYiVVxms/Soc0PG4RAcxQcurGFehc"
        "s4fMw9uBwTihHQAPFbcyDTjZtTMGlaovGaP6xe1H1TMO4GZiJVXGaz9KhzQ8bhEBzFBy6s"
        "YV6FFAhiH0dzP8E0IRZP+oxeL2JkfxiO5v8r7eWnYtMY8d4w7gZmIlVcZrP0qHNDxuEQHM"
        "UHLqxhXoUgoQ1pQreM2tYMR9QaJ7CsSOSJs+Qi5KIzV50DBUYLDjDuBmYiVVxms/Soc0PG"
        "4RAcxQcurGFehdeUg8nHldHqihIknc3OP/QRtBawAyEFY4p0RKlRxnA0MO4GZiJVXGaz9K"
        "hzQ8bhEBzFBy6sYV6FbRY5v48No3N72yRSA9JiYPhS/YTYcUFz\r\n"
               "\r\n");
    MORDOR_TEST_ASSERT(!parser.error());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, TEMPORARY_REDIRECT);
    MORDOR_TEST_ASSERT_EQUAL(response.status.reason, "OK");
    MORDOR_TEST_ASSERT_EQUAL(response.response.location, "/partialObjects/"
        "49ZtbkNPlEEi8T+sQLb5mh9zm1DcyaaRoyHUOC9sEfaKIgLh+eKZNUrqR+j3Iybhx321iz"
        "y3J+Mw7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoXcN18+x4XedNLqc8KhnJtHLXndcKMJu5Cg"
        "xp2BI9NXDDDuBmYiVVxms/Soc0PG4RAcxQcurGFehSY0Wf0fG5eWquA0b0hozVjE4xxyAF"
        "TkSU39Hl3XcsUUMO4GZiJVXGaz9KhzQ8bhEBzFBy6sYV6F9718Fox0OiJ3PqBvo2gr352W"
        "vZBqmEeUV1n0CkcClc0w7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoWapmDUfha0WO9SjTUn4F"
        "Jeht8Gjdy6mYpDqvUbB+3OoDDuBmYiVVxms/Soc0PG4RAcxQcurGFehcefjKkVeAR2HShU"
        "2UpBh5g/89ZP9czSJ8qKSKCPGyHWMO4GZiJVXGaz9KhzQ8bhEBzFBy6sYV6FAig0fJADqV"
        "eInu5RU/pgEXJlZ1MBce/F+rv7MI3g5jgw7gZmIlVcZrP0qHNDxuEQHMUHLqxhXoW4GIxe"
        "C1lnhkTtrAv3jhk17r3ZwL8Fq7CvpUHeAQl/JTDuBmYiVVxms/Soc0PG4RAcxQcurGFehc"
        "s4fMw9uBwTihHQAPFbcyDTjZtTMGlaovGaP6xe1H1TMO4GZiJVXGaz9KhzQ8bhEBzFBy6s"
        "YV6FFAhiH0dzP8E0IRZP+oxeL2JkfxiO5v8r7eWnYtMY8d4w7gZmIlVcZrP0qHNDxuEQHM"
        "UHLqxhXoUgoQ1pQreM2tYMR9QaJ7CsSOSJs+Qi5KIzV50DBUYLDjDuBmYiVVxms/Soc0PG"
        "4RAcxQcurGFehdeUg8nHldHqihIknc3OP/QRtBawAyEFY4p0RKlRxnA0MO4GZiJVXGaz9K"
        "hzQ8bhEBzFBy6sYV6FbRY5v48No3N72yRSA9JiYPhS/YTYcUFz");
}

MORDOR_UNITTEST(HTTP, versionComparison)
{
    Version ver10(1, 0), ver11(1, 1);
    MORDOR_TEST_ASSERT(ver10 == ver10);
    MORDOR_TEST_ASSERT(ver11 == ver11);
    MORDOR_TEST_ASSERT(ver10 != ver11);
    MORDOR_TEST_ASSERT(ver10 <= ver11);
    MORDOR_TEST_ASSERT(ver10 < ver11);
    MORDOR_TEST_ASSERT(ver11 >= ver10);
    MORDOR_TEST_ASSERT(ver11 > ver10);
    MORDOR_TEST_ASSERT(ver10 <= ver10);
    MORDOR_TEST_ASSERT(ver10 >= ver10);
}

static void testQuotingRoundTrip(const std::string &unquoted, const std::string &quoted)
{
    MORDOR_TEST_ASSERT_EQUAL(quote(unquoted), quoted);
    MORDOR_TEST_ASSERT_EQUAL(unquote(quoted), unquoted);
}

MORDOR_UNITTEST(HTTP, quoting)
{
    // Empty string needs to be quoted (otherwise ambiguous)
    testQuotingRoundTrip("", "\"\"");
    // Tokens do not need to be quoted
    testQuotingRoundTrip("token", "token");
    testQuotingRoundTrip("tom", "tom");
    testQuotingRoundTrip("token.non-separator+chars", "token.non-separator+chars");
    // Whitespace is quoted, but not escaped
    testQuotingRoundTrip("multiple words", "\"multiple words\"");
    testQuotingRoundTrip("\tlotsa  whitespace\t",
        "\"\tlotsa  whitespace\t\"");
    // Backslashes and quotes are escaped
    testQuotingRoundTrip("\"", "\"\\\"\"");
    testQuotingRoundTrip("\\", "\"\\\\\"");
    testQuotingRoundTrip("co\\dy", "\"co\\\\dy\"");
    testQuotingRoundTrip("multiple\\ escape\" sequences\\  ",
        "\"multiple\\\\ escape\\\" sequences\\\\  \"");
    // Separators are quoted, but not escaped
    testQuotingRoundTrip("weird < chars >", "\"weird < chars >\"");
    // CTL gets escaped
    testQuotingRoundTrip(std::string("\0", 1), std::string("\"\\\0\"", 4));
    testQuotingRoundTrip("\x7f", "\"\\\x7f\"");
    // > 127 is quoted, but not escaped
    testQuotingRoundTrip("\x80", "\"\x80\"");

    // ETag even quotes tokens
    MORDOR_TEST_ASSERT_EQUAL(quote("token", true), "\"token\"");
}

static void testCommentRoundTrip(const std::string &unquoted, const std::string &quoted)
{
    MORDOR_TEST_ASSERT_EQUAL(quote(unquoted, true, true), quoted);
    MORDOR_TEST_ASSERT_EQUAL(unquote(quoted), unquoted);
}

MORDOR_UNITTEST(HTTP, comments)
{
    // Empty string needs to be quoted (otherwise ambiguous)
    testCommentRoundTrip("", "()");
    // Tokens are quoted
    testCommentRoundTrip("token", "(token)");
    testCommentRoundTrip("token.non-separator+chars", "(token.non-separator+chars)");
    // Whitespace is quoted, but not escaped
    testCommentRoundTrip("multiple words", "(multiple words)");
    testCommentRoundTrip("\tlotsa  whitespace\t",
        "(\tlotsa  whitespace\t)");
    // Backslashes are escaped
    testCommentRoundTrip("\\", "(\\\\)");
    testCommentRoundTrip("co\\dy", "(co\\\\dy)");
    // Quotes are not escaped
    testCommentRoundTrip("\"", "(\")");
    // Separators are quoted, but not escaped
    testCommentRoundTrip("weird < chars >", "(weird < chars >)");
    // CTL gets escaped
    testCommentRoundTrip(std::string("\0", 1), std::string("(\\\0)", 4));
    testCommentRoundTrip("\x7f", "(\\\x7f)");
    // > 127 is quoted, but not escaped
    testCommentRoundTrip("\x80", "(\x80)");
    // Parens are not escaped, if they're matched
    testCommentRoundTrip("()", "(())");
    testCommentRoundTrip("(", "(\\()");
    testCommentRoundTrip(")", "(\\))");
    testCommentRoundTrip("(()", "((\\())");
    testCommentRoundTrip("())", "(()\\))");
    testCommentRoundTrip(")(", "(\\)\\()");
    testCommentRoundTrip("(()))()", "((())\\)())");
}

static void
httpRequest(ServerRequest::ptr request)
{
    switch (request->request().requestLine.method) {
        case GET:
        case HEAD:
        case PUT:
        case POST:
            request->response().entity.contentLength = request->request().entity.contentLength;
            request->response().entity.contentType = request->request().entity.contentType;
            request->response().general.transferEncoding = request->request().general.transferEncoding;
            request->response().status.status = OK;
            request->response().entity.extension = request->request().entity.extension;
            if (request->hasRequestBody()) {
                if (request->request().requestLine.method != HEAD) {
                    if (request->request().entity.contentType.type == "multipart") {
                        Multipart::ptr requestMultipart = request->requestMultipart();
                        Multipart::ptr responseMultipart = request->responseMultipart();
                        for (BodyPart::ptr requestPart = requestMultipart->nextPart();
                            requestPart;
                            requestPart = requestMultipart->nextPart()) {
                            BodyPart::ptr responsePart = responseMultipart->nextPart();
                            responsePart->headers() = requestPart->headers();
                            transferStream(requestPart->stream(), responsePart->stream());
                            responsePart->stream()->close();
                        }
                        responseMultipart->finish();
                    } else {
                        respondStream(request, request->requestStream());
                        return;
                    }
                } else {
                    request->finish();
                }
            } else {
                request->response().entity.contentLength = 0;
                request->finish();
            }
            break;
        default:
            respondError(request, METHOD_NOT_ALLOWED);
            break;
    }
}

static void
doSingleRequest(const char *request, Response &response)
{
    Stream::ptr input(new MemoryStream(Buffer(request)));
    MemoryStream::ptr output(new MemoryStream());
    Stream::ptr stream(new DuplexStream(input, output));
    ServerConnection::ptr conn(new ServerConnection(stream, &httpRequest));
    WorkerPool pool;
    pool.schedule(Fiber::ptr(new Fiber(boost::bind(&ServerConnection::processRequests, conn))));
    pool.dispatch();
    ResponseParser parser(response);
    parser.run(output->buffer());
    MORDOR_TEST_ASSERT(parser.complete());
    MORDOR_TEST_ASSERT(!parser.error());
}

MORDOR_UNITTEST(HTTPServer, badRequest)
{
    Response response;
    doSingleRequest("garbage", response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, BAD_REQUEST);
}

MORDOR_UNITTEST(HTTPServer, close10)
{
    Response response;
    doSingleRequest(
        "GET / HTTP/1.0\r\n"
        "\r\n",
        response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT(response.general.connection.find("close") != response.general.connection.end());
}

MORDOR_UNITTEST(HTTPServer, keepAlive10)
{
    Response response;
    doSingleRequest(
        "GET / HTTP/1.0\r\n"
        "Connection: Keep-Alive\r\n"
        "\r\n",
        response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 0));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT(response.general.connection.find("Keep-Alive") != response.general.connection.end());
    MORDOR_TEST_ASSERT(response.general.connection.find("close") == response.general.connection.end());
}

MORDOR_UNITTEST(HTTPServer, noHost11)
{
    Response response;
    doSingleRequest(
        "GET / HTTP/1.1\r\n"
        "\r\n",
        response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, BAD_REQUEST);
}

MORDOR_UNITTEST(HTTPServer, close11)
{
    Response response;
    doSingleRequest(
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "Connection: close\r\n"
        "\r\n",
        response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT(response.general.connection.find("close") != response.general.connection.end());
}

MORDOR_UNITTEST(HTTPServer, keepAlive11)
{
    Response response;
    doSingleRequest(
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n",
        response);
    MORDOR_TEST_ASSERT_EQUAL(response.status.ver, Version(1, 1));
    MORDOR_TEST_ASSERT_EQUAL(response.status.status, OK);
    MORDOR_TEST_ASSERT(response.general.connection.find("close") == response.general.connection.end());
}

static void
disconnectDuringResponseServer(const URI &uri, ServerRequest::ptr request)
{
    Stream::ptr random(new RandomStream());
    random.reset(new LimitedStream(random, 4096));
    request->response().status.status = OK;
    request->response().general.transferEncoding.push_back("chunked");
    Stream::ptr response = request->responseStream();
    response->flush();
    // Yield so that the request can be cancelled while the response is in progress
    Scheduler::getThis()->schedule(Fiber::getThis());
    Scheduler::getThis()->yieldTo();
    transferStream(random, response);
    MORDOR_TEST_ASSERT_EXCEPTION(response->flush(), BrokenPipeException);
}

MORDOR_UNITTEST(HTTPServer, disconnectDuringResponse)
{
    WorkerPool pool;
    MockConnectionBroker server(&disconnectDuringResponseServer);
    BaseRequestBroker requestBroker(ConnectionBroker::ptr(&server, &nop<ConnectionBroker *>));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "http://localhost/";

    ClientRequest::ptr request = requestBroker.request(requestHeaders);
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Don't read the response body
    request->cancel(true);
}

MORDOR_UNITTEST(HTTPClient, emptyRequest)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);

    // No more requests possible, because we used Connection: close
    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders),
        ConnectionVoluntarilyClosedException);
}

MORDOR_UNITTEST(HTTPClient, pipelinedSynchronousRequests)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request1 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), 0);

    requestHeaders.general.connection.insert("close");
    ClientRequest::ptr request2 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n"
        "GET / HTTP/1.1\r\n"
        "Connection: close\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), 0);

    // No more requests possible, even pipelined ones, because we used
    // Connection: close
    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders),
        ConnectionVoluntarilyClosedException);

    MORDOR_TEST_ASSERT_EQUAL(request1->response().status.status, OK);
    // Can't test for if half of the stream has been consumed here, because it
    // will be in a buffer somewhere
    MORDOR_TEST_ASSERT_EQUAL(request2->response().status.status, OK);
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), responseStream->size());
}

#ifdef DEBUG
MORDOR_UNITTEST(HTTPClient, pipelinedSynchronousRequestsAssertion)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request1 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), 0);

    requestHeaders.general.connection.insert("close");
    ClientRequest::ptr request2 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n"
        "GET / HTTP/1.1\r\n"
        "Connection: close\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), 0);

    // No more requests possible, even pipelined ones, because we used
    // Connection: close
    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders),
        ConnectionVoluntarilyClosedException);

    MORDOR_TEST_ASSERT_EQUAL(request1->response().status.status, OK);
    // We're in a single fiber, and we haven't finished the previous response,
    // so the scheduler will exit when this tries to block, returning
    // immediately, and triggering an assertion that request2 isn't the current
    // response
    IOManager ioManager;
    MORDOR_TEST_ASSERT_ASSERTED(request2->response());
}
#endif

MORDOR_UNITTEST(HTTPClient, emptyResponseBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(response->size(), 0);

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "");

    response.reset();
    MORDOR_ASSERT(!request->responseStream());
}

MORDOR_UNITTEST(HTTPClient, incompleteResponseBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Leng")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
}

MORDOR_UNITTEST(HTTPClient, simpleResponseBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(response->size(), 5);

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "hello");

    response.reset();
    MORDOR_TEST_ASSERT(!request->responseStream());
}

MORDOR_UNITTEST(HTTPClient, readPastEof)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());

    // Verify response itself
    MemoryStream responseBody;
    Stream::ptr response = request->responseStream();
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "hello");
    Buffer buf;
    // Read EOF a few times just to be sure nothing asplodes
    MORDOR_TEST_ASSERT_EQUAL(response->read(buf, 10), 0u);
    MORDOR_TEST_ASSERT_EQUAL(response->read(buf, 10), 0u);
    MORDOR_TEST_ASSERT_EQUAL(response->read(buf, 10), 0u);
}

MORDOR_UNITTEST(HTTPClient, chunkedResponseBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\n"
        "hello"
        "\r\n"
        "0\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(!response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "hello");
}

MORDOR_UNITTEST(HTTPClient, trailerResponse)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(!response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "");

    // Trailer!
    MORDOR_TEST_ASSERT_EQUAL(request->responseTrailer().contentType.type, "text");
    MORDOR_TEST_ASSERT_EQUAL(request->responseTrailer().contentType.subtype, "plain");
}

MORDOR_UNITTEST(HTTPClient, simpleRequestBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();
    // Verify stream characteristics
    MORDOR_TEST_ASSERT(!requestBody->supportsRead());
    MORDOR_TEST_ASSERT(requestBody->supportsWrite());
    MORDOR_TEST_ASSERT(!requestBody->supportsSeek());
    MORDOR_TEST_ASSERT(requestBody->supportsSize());
    MORDOR_TEST_ASSERT(!requestBody->supportsTruncate());
    MORDOR_TEST_ASSERT(!requestBody->supportsFind());
    MORDOR_TEST_ASSERT(!requestBody->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(requestBody->size(), 5);

    // Force a flush (of the headers)
    requestBody->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n");

    // Write the body
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("hello"), 5u);
    requestBody->close();
    MORDOR_TEST_ASSERT_EQUAL(requestBody->size(), 5);

    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello");

    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
}

namespace {
struct DummyException {};
}

static void throwDummyException()
{
    MORDOR_THROW_EXCEPTION(DummyException());
}

static void throwSocketException()
{
    MORDOR_THROW_EXCEPTION(ConnectionResetException());
}

MORDOR_UNITTEST(HTTPClient, simpleRequestBodyExceptionInStream)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    TestStream::ptr testStream(new TestStream(requestStream));
    testStream->onWrite(&throwSocketException, 200);
    testStream->onClose(boost::bind(&throwDummyException));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, testStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 500000;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();
    // Verify stream characteristics
    MORDOR_TEST_ASSERT(!requestBody->supportsRead());
    MORDOR_TEST_ASSERT(requestBody->supportsWrite());
    MORDOR_TEST_ASSERT(!requestBody->supportsSeek());
    MORDOR_TEST_ASSERT(requestBody->supportsSize());
    MORDOR_TEST_ASSERT(!requestBody->supportsTruncate());
    MORDOR_TEST_ASSERT(!requestBody->supportsFind());
    MORDOR_TEST_ASSERT(!requestBody->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(requestBody->size(), 500000);

    // Force a flush (of the headers)
    requestBody->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 500000\r\n"
        "\r\n");

    // Write the body
    RandomStream randomStream;
    MORDOR_TEST_ASSERT_EXCEPTION(transferStream(randomStream, requestBody, 500000), ConnectionResetException);
}

MORDOR_UNITTEST(HTTPClient, multipleCloseRequestBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();

    // Write the body
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("hello"), 5u);
    // Do it multiple times, to make sure nothing asplodes
    requestBody->close();
    requestBody->close();
    requestBody->close();
}

MORDOR_UNITTEST(HTTPClient, underflowRequestBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();

    // Write the body
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("hel"), 3u);
#ifdef DEBUG
    MORDOR_TEST_ASSERT_ASSERTED(requestBody->close());
#endif
}

MORDOR_UNITTEST(HTTPClient, overflowRequestBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();

    // Write the body
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("helloworld"), 5u);
    MORDOR_TEST_ASSERT_EXCEPTION(requestBody->write("hello", 5), WriteBeyondEofException);
}

MORDOR_UNITTEST(HTTPClient, chunkedRequestBody)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.general.transferEncoding.push_back(ValueWithParameters("chunked"));

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();
    // Verify stream characteristics
    MORDOR_TEST_ASSERT(!requestBody->supportsRead());
    MORDOR_TEST_ASSERT(requestBody->supportsWrite());
    MORDOR_TEST_ASSERT(!requestBody->supportsSeek());
    MORDOR_TEST_ASSERT(!requestBody->supportsSize());
    MORDOR_TEST_ASSERT(!requestBody->supportsTruncate());
    MORDOR_TEST_ASSERT(!requestBody->supportsFind());
    MORDOR_TEST_ASSERT(!requestBody->supportsUnread());

    // Force a flush (of the headers)
    requestBody->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n");

    // Write the body
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("hello"), 5u);
    MORDOR_TEST_ASSERT_EQUAL(requestBody->write("world"), 5u);
    requestBody->close();

    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\n"
        "hello"
        "\r\n"
        "5\r\n"
        "world"
        "\r\n"
        "0\r\n"
        // No trailers
        "\r\n");

    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
}

MORDOR_UNITTEST(HTTPClient, simpleRequestPartialWrites)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    TestStream::ptr testStream(new TestStream(duplexStream));
    testStream->maxReadSize(10);
    testStream->maxWriteSize(10);
    ClientConnection::ptr conn(new ClientConnection(testStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = GET;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);

    // Force a flush (of the headers)
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");

    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
}

static void pipelinedRequests(ClientConnection::ptr conn,
    int &sequence)
{
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 2);

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request2 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 4);

    MORDOR_TEST_ASSERT_EQUAL(request2->response().status.status, NOT_FOUND);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 6);
}

MORDOR_UNITTEST(HTTPClient, pipelinedRequests)
{
    WorkerPool pool;
    int sequence = 1;

    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 0\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";
    requestHeaders.entity.contentLength = 7;

    ClientRequest::ptr request1 = conn->request(requestHeaders);
    
    // Start the second request, which will yield to us when it can't use the conn
    Fiber::ptr request2Fiber(new Fiber(boost::bind(&pipelinedRequests,
        conn, boost::ref(sequence))));
    pool.schedule(request2Fiber);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 3);

    MORDOR_TEST_ASSERT_EQUAL(request1->requestStream()->write("hello\r\n"), 7u);
    request1->requestStream()->close();

    // Nothing has been sent to the server yet (it's buffered up), because
    // there is a pipelined request waiting, and we only flush after
    // the last request
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);

    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 5);
    
    // Both requests have been sent now (flush()es after last request)
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "Content-Length: 7\r\n"
        "\r\n"
        "hello\r\n"
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    
    // Nothing has been read yet
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), 0);

    MORDOR_TEST_ASSERT_EQUAL(request1->response().status.status, OK);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 7);

    // Both responses have been read now
    MORDOR_TEST_ASSERT_EQUAL(responseStream->tell(), responseStream->size());
}

MORDOR_UNITTEST(HTTPClient, missingTrailerResponse)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(!response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());

    // Trailer hasn't been read yet
#ifdef DEBUG
    MORDOR_TEST_ASSERT_ASSERTED(request->responseTrailer());
#endif

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "");

    // Missing trailer
    MORDOR_TEST_ASSERT_EXCEPTION(request->responseTrailer(), IncompleteMessageHeaderException);
}

MORDOR_UNITTEST(HTTPClient, badTrailerResponse)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "0\r\n"
        "Content-Type: garbage\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(!response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());

    // Trailer hasn't been read yet
#ifdef DEBUG
    MORDOR_TEST_ASSERT_ASSERTED(request->responseTrailer());
#endif

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "");

    // Missing trailer
    MORDOR_TEST_ASSERT_EXCEPTION(request->responseTrailer(), BadMessageHeaderException);
}

MORDOR_UNITTEST(HTTPClient, cancelRequestSingle)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    request->cancel();

    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders), PriorRequestFailedException);
}

MORDOR_UNITTEST(HTTPClient, cancelResponseSingle)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.request.host = "garbage";

    ClientRequest::ptr request1 = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.1\r\n"
        "Host: garbage\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request1->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request1->hasResponseBody());

    ClientRequest::ptr request2 = conn->request(requestHeaders);

    request1->cancel(true);

    MORDOR_TEST_ASSERT_EXCEPTION(request2->ensureResponse(), PriorRequestFailedException);
    MORDOR_TEST_ASSERT_EXCEPTION(request2->ensureResponse(), PriorRequestFailedException);
    MORDOR_TEST_ASSERT_EXCEPTION(request2->ensureResponse(), PriorRequestFailedException);

    /* This assertion no longer holds, because instead of closing the connection
       (gracefully), we cancelRead and cancelWrite to avoid possible exceptions.
       DuplexStream doesn't do anything when cancelling, so the data can still
       be read

    // Verify response can't be read (exception; when using a real socket it might let us
    // read to EOF)
    MemoryStream responseBody;
    MORDOR_TEST_ASSERT_EXCEPTION(transferStream(request1->responseStream(), responseBody),
        BrokenPipeException);
    */

    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders), PriorRequestFailedException);
}

MORDOR_UNITTEST(HTTPClient, simpleRequestAbandoned)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.method = PUT;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    // Nothing has been flushed yet
    MORDOR_TEST_ASSERT_EQUAL(requestStream->size(), 0);
    Stream::ptr requestBody = request->requestStream();
    // Verify stream characteristics
    MORDOR_TEST_ASSERT(!requestBody->supportsRead());
    MORDOR_TEST_ASSERT(requestBody->supportsWrite());
    MORDOR_TEST_ASSERT(!requestBody->supportsSeek());
    MORDOR_TEST_ASSERT(requestBody->supportsSize());
    MORDOR_TEST_ASSERT(!requestBody->supportsTruncate());
    MORDOR_TEST_ASSERT(!requestBody->supportsFind());
    MORDOR_TEST_ASSERT(!requestBody->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(requestBody->size(), 5);

    // Force a flush (of the headers)
    requestBody->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "PUT / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n");

    request.reset();
    // Write the body - not allowed because we abandoned the request
    MORDOR_TEST_ASSERT_ASSERTED(requestBody->write("hello"));
}

MORDOR_UNITTEST(HTTPClient, simpleResponseAbandoned)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    // Response stream has a ref to the request
    request.reset();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(response->size(), 5);

    Buffer buf;
    MORDOR_TEST_ASSERT_EQUAL(response->read(buf, 3), 3u);
    MORDOR_TEST_ASSERT(buf == "hel");
    // This should be okay, but will kill the connection
    response.reset();
    MORDOR_TEST_ASSERT_EXCEPTION(conn->request(requestHeaders),
        PriorRequestFailedException);
}

MORDOR_UNITTEST(HTTPClient, simpleResponseAbandonRequest)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    request.reset();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(response->size(), 5);

    // Verify response itself
    MemoryStream responseBody;
    transferStream(response, responseBody);
    MORDOR_TEST_ASSERT(responseBody.buffer() == "hello");
}

MORDOR_UNITTEST(HTTPClient, simpleResponseAbandonStream)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    request.reset();
    MORDOR_TEST_ASSERT(response->supportsRead());
    MORDOR_TEST_ASSERT(!response->supportsWrite());
    MORDOR_TEST_ASSERT(!response->supportsSeek());
    MORDOR_TEST_ASSERT(response->supportsSize());
    MORDOR_TEST_ASSERT(!response->supportsTruncate());
    MORDOR_TEST_ASSERT(!response->supportsFind());
    MORDOR_TEST_ASSERT(!response->supportsUnread());
    MORDOR_TEST_ASSERT_EQUAL(response->size(), 5);

    // Everything should clean up properly
    response.reset();
}

MORDOR_UNITTEST(HTTPClient, simpleResponseExceptionInStream)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 250005\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    Stream::ptr randomStream(new RandomStream());
    randomStream.reset(new LimitedStream(randomStream, 250000));
    std::vector<Stream::ptr> streams;
    streams.push_back(responseStream);
    streams.push_back(randomStream);
    Stream::ptr completeResponseStream(new CatStream(streams));
    TestStream::ptr testStream(new TestStream(completeResponseStream));
    testStream->onRead(&throwSocketException, 100000);
    DuplexStream::ptr duplexStream(new DuplexStream(testStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, OK);
    // Verify response characteristics
    MORDOR_TEST_ASSERT(request->hasResponseBody());
    Stream::ptr response = request->responseStream();
    request.reset();
    MORDOR_TEST_ASSERT_EXCEPTION(transferStream(response, NullStream::get()),
        ConnectionResetException);
}

MORDOR_UNITTEST(HTTPClient, emptyResponseCompleteBeforeRequestComplete)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    request->requestStream()->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, HTTP::OK);

    request->requestStream()->write("hello");
    request->requestStream()->close();
}

static void emptyResponseCompleteBeforeRequestCompletePipelinedSecondRequest(
    ClientConnection::ptr conn, ClientRequest::ptr &request2)
{
    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.entity.contentLength = 5;
    request2 = conn->request(requestHeaders);
}

MORDOR_UNITTEST(HTTPClient, emptyResponseCompleteBeforeRequestCompletePipelined)
{
    WorkerPool pool;
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request1 = conn->request(requestHeaders), request2;
    // Has to happen in a separate fiber because we need to force request1 to
    // not flush (because there's already another request in the queue, before
    // this request has completed)
    pool.schedule(boost::bind(&emptyResponseCompleteBeforeRequestCompletePipelinedSecondRequest,
        conn, boost::ref(request2)));
    pool.dispatch();

    MORDOR_TEST_ASSERT_EQUAL(request1->response().status.status, HTTP::OK);
    request1->requestStream()->write("hello");
    request1->requestStream()->close();
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(request2->response().status.status, HTTP::OK);
    request2->requestStream()->write("hello");
    request2->requestStream()->close();
}

MORDOR_UNITTEST(HTTPClient, simpleResponseCompleteBeforeRequestComplete)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    request->requestStream()->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, HTTP::OK);
    MemoryStream response;
    transferStream(request->responseStream(), response);
    MORDOR_ASSERT(response.buffer() == "hello");

    request->requestStream()->write("world");
    request->requestStream()->close();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "world");
}

MORDOR_UNITTEST(HTTPClient, responseFailBeforeRequestComplete)
{
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Len")));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";
    requestHeaders.general.connection.insert("close");
    requestHeaders.entity.contentLength = 5;

    ClientRequest::ptr request = conn->request(requestHeaders);
    request->requestStream()->flush();
    MORDOR_TEST_ASSERT(requestStream->buffer() ==
        "GET / HTTP/1.0\r\n"
        "Connection: close\r\n"
        "Content-Length: 5\r\n"
        "\r\n");
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), IncompleteMessageHeaderException);
}

static void newRequestWhileFlushing(ClientConnection::ptr conn, int &sequence, NotifyStream::ptr notify)
{
    if (notify) {
        notify->notifyOnFlush = NULL;
        MORDOR_TEST_ASSERT_EQUAL(++sequence, 1);
        Scheduler::getThis()->schedule(boost::bind(&newRequestWhileFlushing, conn, boost::ref(sequence), NotifyStream::ptr()));
        Scheduler::getThis()->schedule(Fiber::getThis());
        Scheduler::getThis()->yieldTo();
        return;
    }
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 2);

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 4);
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, HTTP::OK);
}

MORDOR_UNITTEST(HTTPClient, newRequestWhileFlushing)
{
    WorkerPool pool;
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream(Buffer(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n")));
    NotifyStream::ptr notifyStream(new NotifyStream(requestStream));
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, notifyStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    int sequence = 0;
    notifyStream->notifyOnFlush = boost::bind(&newRequestWhileFlushing, conn, boost::ref(sequence), notifyStream);

    Request requestHeaders;
    requestHeaders.requestLine.uri = "/";

    ClientRequest::ptr request = conn->request(requestHeaders);
    MORDOR_TEST_ASSERT_EQUAL(request->response().status.status, HTTP::OK);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 3);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 5);
}

static void
zlibCausesPrematureEOFServer(const URI &uri, ServerRequest::ptr request)
{
    Stream::ptr random(new RandomStream());
    random.reset(new LimitedStream(random, 4096));
    request->response().general.transferEncoding.push_back("deflate");
    request->response().general.transferEncoding.push_back("chunked");
    request->response().status.status = OK;
    transferStream(random, request->responseStream());
    request->responseStream()->close();
}

MORDOR_UNITTEST(HTTPClient, zlibCausesPrematureEOF)
{
    WorkerPool pool;
    MockConnectionBroker server(&zlibCausesPrematureEOFServer);
    BaseRequestBroker requestBroker(ConnectionBroker::ptr(&server, &nop<ConnectionBroker *>));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "http://localhost/";

    ClientRequest::ptr request = requestBroker.request(requestHeaders);
    transferStream(request->responseStream(), NullStream::get());

    request = requestBroker.request(requestHeaders);
    transferStream(request->responseStream(), NullStream::get());
}

static void waitForPriorResponseFailed(ClientRequest::ptr request, int &sequence)
{
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 2);
    MORDOR_TEST_ASSERT_EXCEPTION(request->response(), PriorRequestFailedException);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 4);
}

MORDOR_UNITTEST(HTTPClient, priorResponseFailedPipeline)
{
    WorkerPool pool;
    MemoryStream::ptr requestStream(new MemoryStream());
    MemoryStream::ptr responseStream(new MemoryStream());
    DuplexStream::ptr duplexStream(new DuplexStream(responseStream, requestStream));
    ClientConnection::ptr conn(new ClientConnection(duplexStream));

    Request requestHeaders;
    requestHeaders.requestLine.uri = "http://localhost/";

    ClientRequest::ptr request1 = conn->request(requestHeaders);
    ClientRequest::ptr request2 = conn->request(requestHeaders);
    int sequence = 1;
    pool.schedule(boost::bind(&waitForPriorResponseFailed, request2, boost::ref(sequence)));
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 3);
    request1->cancel(true);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 5);
}
