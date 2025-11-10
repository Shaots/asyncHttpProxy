#include "headers.h"

#include <ranges>
#include <string_view>

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback) {
    auto lines = req | std::views::split("\r\n"sv);

    bool skip_request_line = true;

    for (auto line_range : lines) {
        std::string_view line(line_range.begin(), line_range.end());

        if (line.empty()) {
            break;
        }

        if (skip_request_line) {
            skip_request_line = false;
            continue;
        }

        auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            continue;
        }

        std::string_view name = line.substr(0, colon_pos);

        std::string_view value = line.substr(colon_pos + 1);
        auto value_start =
            std::ranges::find_if(value, [](char c) { return !std::isspace(static_cast<unsigned char>(c)); });

        if (value_start != value.end()) {
            value = std::string_view(&*value_start, std::distance(value_start, value.end()));
        } else {
            value = ""sv;
        }

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
