#include "headers.h"
#include <gtest/gtest.h>

TEST(iterHeaders, Empty) {
    std::string request = "GET / HTTP/1.1\r\n\r\n";
    int count = 0;

    iterHeaders(request, [&](std::string_view name, std::string_view value) { count++; });

    EXPECT_EQ(count, 0);
}

TEST(iterHeaders, SkipRequestLine) {
    std::string request = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    std::vector<std::string> headers;

    iterHeaders(request, [&](std::string_view name, std::string_view value) {
        headers.push_back(std::string(name) + ": " + std::string(value));
    });

    EXPECT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0], "Host: example.com");
}

TEST(iterHeaders, SingleHeader) {
    std::string request = "GET / HTTP/1.1\r\nContent-Type: text/html\r\n\r\n";
    std::vector<std::string> names, values;

    iterHeaders(request, [&](std::string_view name, std::string_view value) {
        names.push_back(std::string(name));
        values.push_back(std::string(value));
    });

    EXPECT_EQ(names.size(), 1);
    EXPECT_EQ(values.size(), 1);
    EXPECT_EQ(names[0], "Content-Type");
    EXPECT_EQ(values[0], "text/html");
}

TEST(iterHeaders, MultipleHeaders) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: example.com\r\n"
                          "User-Agent: test-agent\r\n"
                          "Accept: */*\r\n"
                          "\r\n";

    std::vector<std::string> names;

    iterHeaders(request, [&](std::string_view name, std::string_view value) { names.push_back(std::string(name)); });

    EXPECT_EQ(names.size(), 3);
    EXPECT_EQ(names[0], "Host");
    EXPECT_EQ(names[1], "User-Agent");
    EXPECT_EQ(names[2], "Accept");
}

TEST(iterHeaders, MultipleSameHeaders) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "X-Custom: value1\r\n"
                          "X-Custom: value2\r\n"
                          "X-Custom: value3\r\n"
                          "\r\n";

    std::vector<std::string> values;

    iterHeaders(request, [&](std::string_view name, std::string_view value) {
        if (name == "X-Custom") {
            values.push_back(std::string(value));
        }
    });

    EXPECT_EQ(values.size(), 3);
    EXPECT_EQ(values[0], "value1");
    EXPECT_EQ(values[1], "value2");
    EXPECT_EQ(values[2], "value3");
}

TEST(iterHeaders, HeadersWithSpaces) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Content-Type:   text/html; charset=utf-8  \r\n"
                          "Content-Length:   123  \r\n"
                          "\r\n";

    std::vector<std::string> values;

    iterHeaders(request, [&](std::string_view name, std::string_view value) { values.push_back(std::string(value)); });

    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values[0], "text/html; charset=utf-8  ");
    EXPECT_EQ(values[1], "123  ");
}

TEST(findHostPort, Simple) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: example.com:8080\r\n"
                          "\r\n";

    auto [host, port] = findHostPort(request);

    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, NoHost) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "User-Agent: test\r\n"
                          "\r\n";

    auto [host, port] = findHostPort(request);

    EXPECT_TRUE(host.empty());
    EXPECT_EQ(port, "80");
}

TEST(findContentLength, Simple) {
    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: 1024\r\n"
                           "\r\n";

    auto length = findContentLength(response);

    EXPECT_TRUE(length.has_value());
    EXPECT_EQ(length.value(), 1024);
}

TEST(findContentLength, NoContentLength) {
    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "\r\n";

    auto length = findContentLength(response);

    EXPECT_FALSE(length.has_value());
}
