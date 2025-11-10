#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback) {
    size_t pos = req.find("\r\n");
    if (pos == std::string_view::npos) {
        return;
    }

    // Пропускаем request line и начинаем с headers
    size_t start = pos + 2;  // +2 для пропуска \r\n

    while (start < req.length()) {
        size_t end = req.find("\r\n", start);
        if (end == std::string_view::npos) {
            break;
        }

        if (end == start) {
            break;
        }

        std::string_view header_line = req.substr(start, end - start);

        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string_view::npos) {
            std::string_view name = header_line.substr(0, colon_pos);

            size_t value_start = colon_pos + 1;
            while (value_start < header_line.length() &&
                   std::isspace(static_cast<unsigned char>(header_line[value_start]))) {
                value_start++;
            }
            std::string_view value = header_line.substr(value_start);

            callback(name, value);
        }

        start = end + 2;
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::string host;
    std::string port = "80";  // default HTTP port

    const std::string_view host_header = "host";
    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        if (name.length() == host_header.length()) {
            std::string name_lower;
            name_lower.reserve(name.length());
            std::transform(name.begin(), name.end(), std::back_inserter(name_lower),
                           [](unsigned char c) { return std::tolower(c); });
            if (name_lower == host_header) {
                std::string host_value(value);

                size_t colon_pos = host_value.find(':');
                if (colon_pos != std::string::npos) {
                    host = host_value.substr(0, colon_pos);
                    port = host_value.substr(colon_pos + 1);
                } else {
                    host = host_value;
                }
            }
        }
    });

    return {host, port};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;
    const std::string_view content_length_header = "content-length";
    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        if (name.length() == content_length_header.length()) {
            std::string name_lower;
            name_lower.reserve(name.length());
            std::transform(name.begin(), name.end(), std::back_inserter(name_lower),
                           [](unsigned char c) { return std::tolower(c); });
            if (name_lower == content_length_header) {
                try {
                    result = std::stoul(std::string(value));
                } catch (const std::exception &) {
                }
            }
        }
    });

    return result;
}
