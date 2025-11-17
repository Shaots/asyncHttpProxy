#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <print>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::dynamic_buffer;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";

awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    tcp::socket server_socket(io_service);

    try {
        std::println("Starting new proxy session");

        boost::asio::streambuf client_request_buf;
        std::size_t headers_size =
            co_await async_read_until(client_socket, client_request_buf, delimiter, use_awaitable);

        std::string client_headers = boost::asio::buffer_cast<const char *>(client_request_buf.data());
        client_headers = client_headers.substr(0, headers_size);

        std::println("Received client headers ({} bytes)", client_headers.length());
        std::println("{}", client_headers);

        auto [host, port] = findHostPort(client_headers);

        if (host.empty()) {
            std::cerr << "Error: No Host header found in request\n";
            co_return;
        }

        std::println("Target host: {}:{}", host, port);

        tcp::resolver resolver(io_service);
        auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);
        co_await boost::asio::async_connect(server_socket, endpoints, use_awaitable);

        std::println("Connected to target server: {}:{}", host, port);

        co_await async_write(server_socket, buffer(client_headers), use_awaitable);
        client_request_buf.consume(headers_size);

        std::println("Forwarded client headers to server");

        auto request_content_length = findContentLength(client_headers);
        if (request_content_length.has_value() && request_content_length.value() > 0) {
            std::size_t body_size = request_content_length.value();
            std::println("Forwarding request body ({} bytes)", body_size);

            boost::asio::streambuf body_buf;
            while (body_buf.size() < body_size) {
                std::size_t bytes_to_read = body_size - body_buf.size();
                std::size_t bytes_read =
                    co_await client_socket.async_read_some(body_buf.prepare(bytes_to_read), use_awaitable);
                body_buf.commit(bytes_read);
            }

            co_await async_write(server_socket, body_buf.data(), use_awaitable);
            body_buf.consume(body_size);

            std::println("Request body forwarded successfully");
        }

        boost::asio::streambuf server_response_buf;
        std::size_t response_headers_size =
            co_await async_read_until(server_socket, server_response_buf, delimiter, use_awaitable);

        std::string server_headers = boost::asio::buffer_cast<const char *>(server_response_buf.data());
        server_headers = server_headers.substr(0, response_headers_size);

        std::println("Received server headers ({} bytes)", server_headers.length());
        std::println("{}", server_headers);

        auto response_content_length = findContentLength(server_headers);
        std::size_t response_body_size = 0;

        if (response_content_length.has_value()) {
            response_body_size = response_content_length.value();
            std::println("Response body size: {}", response_body_size);
        } else {
            std::println("No Content-Length in response, using chunked transfer");
        }

        co_await async_write(client_socket, buffer(server_headers), use_awaitable);
        server_response_buf.consume(response_headers_size);

        std::println("Forwarded server headers to client");

        if (response_content_length.has_value() && response_body_size > 0) {
            std::size_t total_body_received = 0;

            while (total_body_received < response_body_size) {
                constexpr std::size_t max_read_bytes = 4096;
                std::size_t bytes_to_read = std::min(max_read_bytes, response_body_size - total_body_received);

                boost::asio::streambuf chunk_buf;
                std::size_t bytes_read =
                    co_await server_socket.async_read_some(chunk_buf.prepare(bytes_to_read), use_awaitable);
                chunk_buf.commit(bytes_read);

                co_await async_write(client_socket, chunk_buf.data(), use_awaitable);
                chunk_buf.consume(bytes_read);

                total_body_received += bytes_read;

                std::println("Transferred {}/{} bytes", total_body_received, response_body_size);
            }

            std::println("Response body transfer completed");

        } else {
            std::println("Starting chunked response transfer");

            error_code ec;
            std::size_t total_transferred = 0;

            while (!ec) {
                boost::asio::streambuf chunk_buf;

                std::size_t bytes_read = co_await server_socket.async_read_some(chunk_buf.prepare(4096), use_awaitable);

                if (bytes_read == 0) {
                    break;
                }

                chunk_buf.commit(bytes_read);

                co_await async_write(client_socket, chunk_buf.data(), use_awaitable);
                chunk_buf.consume(bytes_read);

                total_transferred += bytes_read;

                constexpr std::size_t hundredKB = 102400;
                if (total_transferred % hundredKB == 0) {
                    std::println("Chunked transfer: {} bytes so far", total_transferred);
                }
            }

            std::println("Chunked transfer completed, total: {} bytes", total_transferred);
        }

        std::println("Proxy session completed successfully");

    } catch (const std::exception &e) {
        std::cerr << "Session exception: " << e.what() << std::endl;
    }

    if (client_socket.is_open()) {
        client_socket.close();
    }
    if (server_socket.is_open()) {
        server_socket.close();
    }

    std::println("Proxy session ended\n");
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

private:
    void do_accept() {
        auto new_socket = std::make_shared<tcp::socket>(io_service_);

        acceptor_.async_accept(*new_socket, [this, new_socket](error_code ec) {
            if (!ec) {
                auto remote_endpoint = new_socket->remote_endpoint();
                std::println("New connection from {}:{}", remote_endpoint.address().to_string(),
                             remote_endpoint.port());
                co_spawn(io_service_, session(std::move(*new_socket), io_service_), boost::asio::detached);
            } else {
                std::cerr << "Accept error: " << ec.message() << std::endl;
            }

            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        short port = static_cast<short>(std::atoi(argv[1]));
        if (port <= 0) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        }
        std::println("Starting proxy server on port {}", port);

        io_service io_service(1);
        Server server(io_service, port);
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
