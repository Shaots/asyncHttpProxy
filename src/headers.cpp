#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback) {
    auto headers =
        req | std::views::split("\r\n"sv) |
        std::views::transform([](auto line_range) { return std::string_view(line_range.begin(), line_range.end()); }) |
        std::views::drop(1) | std::views::take_while([](std::string_view line) { return !line.empty(); }) |
        std::views::filter([](std::string_view line) { return line.find(':') != std::string_view::npos; }) |
        std::views::transform([](std::string_view line) {
            auto colon_pos = line.find(':');
            std::string_view name = line.substr(0, colon_pos);

            std::string_view value = line.substr(colon_pos + 1);
            auto value_trimmed =
                value | std::views::drop_while([](char c) { return std::isspace(static_cast<unsigned char>(c)); });

            if (value_trimmed.begin() != value_trimmed.end()) {
                value = std::string_view(&*value_trimmed.begin(),
                                         std::distance(value_trimmed.begin(), value_trimmed.end()));
            } else {
                value = ""sv;
            }

            return std::make_pair(name, value);
        });

    for (auto [name, value] : headers) {
        callback(name, value);
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::string host;
    std::string port = "80";  // default HTTP port

    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        auto name_lower =
            name | std::views::transform([](char c) { return std::tolower(static_cast<unsigned char>(c)); });

        std::string name_lower_str(name_lower.begin(), name_lower.end());

        if (name_lower_str == "host") {
            auto host_parts = value | std::views::split(':');

            auto it = host_parts.begin();
            if (it != host_parts.end()) {
                std::string_view host_range(*it);
                host = std::string(host_range.begin(), host_range.end());

                if (++it != host_parts.end()) {
                    std::string_view port_range(*it);
                    port = std::string(port_range.begin(), port_range.end());
                }
            }
        }
    });

    return {host, port};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;

    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        auto name_lower =
            name | std::views::transform([](char c) { return std::tolower(static_cast<unsigned char>(c)); });

        std::string name_lower_str(name_lower.begin(), name_lower.end());

        if (name_lower_str == "content-length") {
            try {
                result = std::stoul(std::string(value));
            } catch (const std::exception &) {
            }
        }
    });
    return result;
}
