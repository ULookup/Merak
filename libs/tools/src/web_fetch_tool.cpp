#include <merak/web_fetch_tool.hpp>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <future>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace merak::tools {

static constexpr size_t kMaxResponseBytes = 5 * 1024 * 1024; // 5 MB

// --- SSRF protection ---

static bool is_private_ip(const std::string& ip_str) {
    // IPv4 private ranges
    struct IPv4Range {
        uint32_t net;
        uint32_t mask;
    };
    static const std::vector<IPv4Range> ipv4_private = {
        {0x00000000, 0xFF000000},   // 0.0.0.0/8
        {0x0A000000, 0xFF000000},   // 10.0.0.0/8
        {0x7F000000, 0xFF000000},   // 127.0.0.0/8
        {0xA9FE0000, 0xFFFF0000},   // 169.254.0.0/16
        {0xAC100000, 0xFFF00000},   // 172.16.0.0/12
        {0xC0A80000, 0xFFFF0000},   // 192.168.0.0/16
    };

    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str.c_str(), &addr) == 1) {
        uint32_t ip = ntohl(addr.s_addr);
        for (const auto& range : ipv4_private) {
            if ((ip & range.mask) == range.net) return true;
        }
    }

    // IPv6 loopback / link-local
    if (ip_str == "::1") return true;
    struct in6_addr addr6;
    if (inet_pton(AF_INET6, ip_str.c_str(), &addr6) == 1) {
        // fc00::/7 (ULA)
        if ((addr6.s6_addr[0] & 0xFE) == 0xFC) return true;
        // fe80::/10 (link-local)
        if (addr6.s6_addr[0] == 0xFE && (addr6.s6_addr[1] & 0xC0) == 0x80) return true;
    }

    return false;
}

static std::pair<bool, std::string> check_url_safe(const std::string& url_str) {
    // Parse scheme
    auto scheme_end = url_str.find("://");
    if (scheme_end == std::string::npos) {
        return {false, "Missing scheme in URL"};
    }
    std::string scheme = url_str.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (scheme != "http" && scheme != "https") {
        return {false, "Only http and https schemes are allowed"};
    }

    // Extract host
    auto host_start = scheme_end + 3;
    auto path_start = url_str.find('/', host_start);
    std::string host = url_str.substr(host_start, path_start - host_start);

    // Strip port if present
    auto port_pos = host.find(':');
    std::string hostname = host.substr(0, port_pos);

    // DNS resolve
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    int gai_rc = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (gai_rc != 0) {
        return {false, "DNS resolution failed for host: " + hostname};
    }

    bool safe = true;
    std::string bad_ip;
    for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        char ip_buf[INET6_ADDRSTRLEN];
        if (rp->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in*)rp->ai_addr)->sin_addr, ip_buf, sizeof(ip_buf));
        } else if (rp->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6*)rp->ai_addr)->sin6_addr, ip_buf, sizeof(ip_buf));
        } else {
            continue;
        }
        if (is_private_ip(ip_buf)) {
            safe = false;
            bad_ip = ip_buf;
            break;
        }
    }
    freeaddrinfo(res);

    if (!safe) {
        return {false, "SSRF blocked: resolved to private IP " + bad_ip};
    }

    return {true, ""};
}

// --- Basic HTML to Markdown conversion ---

static std::string strip_tags(const std::string& html) {
    // Remove script and style blocks
    std::string cleaned;
    {
        std::regex script_re(R"(<script[^>]*>[\s\S]*?</script>)", std::regex::icase);
        cleaned = std::regex_replace(html, script_re, "");
    }
    {
        std::regex style_re(R"(<style[^>]*>[\s\S]*?</style>)", std::regex::icase);
        cleaned = std::regex_replace(cleaned, style_re, "");
    }
    return cleaned;
}

static std::string html_to_markdown(const std::string& html) {
    std::string text = strip_tags(html);

    // Convert headings
    for (int i = 6; i >= 1; --i) {
        std::regex h_re("<h" + std::to_string(i) + "[^>]*>(.*?)</h" + std::to_string(i) + ">",
                        std::regex::icase);
        std::string marker(i, '#');
        text = std::regex_replace(text, h_re, "\n\n" + marker + " $1\n\n");
    }

    // Convert paragraphs
    text = std::regex_replace(text, std::regex("<p[^>]*>", std::regex::icase), "\n\n");
    text = std::regex_replace(text, std::regex("</p>", std::regex::icase), "\n\n");

    // Convert list items
    text = std::regex_replace(text, std::regex("<li[^>]*>", std::regex::icase), "\n- ");
    text = std::regex_replace(text, std::regex("</li>", std::regex::icase), "");

    // Convert links
    text = std::regex_replace(text,
        std::regex("<a[^>]*href=\"([^\"]*)\"[^>]*>([^<]*)</a>", std::regex::icase),
        "[$2]($1)");

    // Convert <br> and <br/>
    text = std::regex_replace(text, std::regex("<br\\s*/?\\s*>", std::regex::icase), "\n");

    // Remove remaining HTML tags
    text = std::regex_replace(text, std::regex("<[^>]*>"), "");

    // Decode common HTML entities
    text = std::regex_replace(text, std::regex("&amp;"), "&");
    text = std::regex_replace(text, std::regex("&lt;"), "<");
    text = std::regex_replace(text, std::regex("&gt;"), ">");
    text = std::regex_replace(text, std::regex("&quot;"), "\"");
    text = std::regex_replace(text, std::regex("&#39;"), "'");
    text = std::regex_replace(text, std::regex("&nbsp;"), " ");

    // Collapse whitespace
    text = std::regex_replace(text, std::regex("\n{3,}"), "\n\n");

    return text;
}

// --- Main Tool ---

ToolSpec WebFetchTool::spec() const {
    ToolSpec s;
    s.name = "web_fetch";
    s.description = "Fetch URL content — web pages, APIs, documentation";
    s.source = "builtin";
    s.category = Category::Consultative;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "url": {
                "type": "string",
                "description": "The URL to fetch"
            },
            "max_chars": {
                "type": "integer",
                "description": "Maximum characters to return",
                "default": 8000
            },
            "format": {
                "type": "string",
                "enum": ["text", "markdown"],
                "description": "Output format: text or markdown",
                "default": "markdown"
            }
        },
        "required": ["url"]
    })";
    return s;
}

ToolMeta WebFetchTool::meta() const {
    ToolMeta m;
    m.name = "web_fetch";
    m.description = "Fetch URL content — web pages, APIs, documentation";
    m.triggers = {"fetch", "url", "web", "http", "download", "api call", "curl"};
    m.pinned = false;
    m.intents = {IntentType::Network};
    m.scope = Scope::External;
    m.schema_tokens = 25;
    return m;
}

PermissionLevel WebFetchTool::permission() const {
    return PermissionLevel::ask;
}

std::future<ToolResult> WebFetchTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto url_str = args.value("url", "");
            auto max_chars = args.value("max_chars", 8000);
            auto format = args.value("format", "markdown");

            // SSRF check
            auto [safe, error_msg] = check_url_safe(url_str);
            if (!safe) {
                result.output = error_msg;
                result.is_error = true;
                return result;
            }

            // Parse URL for httplib
            auto scheme_end = url_str.find("://");
            std::string scheme = url_str.substr(0, scheme_end);
            std::string host_path = url_str.substr(scheme_end + 3);
            auto path_start = host_path.find('/');
            std::string host = host_path.substr(0, path_start);
            std::string path = (path_start == std::string::npos) ? "/" : host_path.substr(path_start);

            bool use_ssl = (scheme == "https");
            httplib::Client cli(host);
            if (use_ssl) {
                cli.enable_server_certificate_verification(true);
            }
            cli.set_follow_location(true);
            cli.set_connection_timeout(10);
            cli.set_read_timeout(30);

            // Limit redirects: httplib follows by default; we cap via max_redirects if needed.
            // httplib's set_follow_location(true) follows up to some internal limit; we accept that.

            auto res = cli.Get(path);
            if (!res) {
                result.output = "HTTP request failed: " + to_string(res.error());
                result.is_error = true;
                return result;
            }

            if (res->status != 200) {
                result.output = "HTTP " + std::to_string(res->status);
                result.is_error = true;
                return result;
            }

            // Size limit
            if (res->body.size() > kMaxResponseBytes) {
                result.output = "Response too large: " + std::to_string(res->body.size()) +
                                " bytes (max " + std::to_string(kMaxResponseBytes) + ")";
                result.is_error = true;
                return result;
            }

            // Content-Type filtering
            auto ct = res->get_header_value("Content-Type");
            bool acceptable = false;
            if (ct.find("text/html") != std::string::npos) acceptable = true;
            if (ct.find("text/plain") != std::string::npos) acceptable = true;
            if (ct.find("application/json") != std::string::npos) acceptable = true;

            std::string output_text;
            if (acceptable && ct.find("text/html") != std::string::npos) {
                if (format == "markdown") {
                    output_text = html_to_markdown(res->body);
                } else {
                    output_text = strip_tags(res->body);
                }
            } else if (acceptable) {
                output_text = res->body;
            } else {
                result.output = "Unsupported Content-Type: " + ct;
                result.is_error = true;
                return result;
            }

            // Truncate
            if (static_cast<int>(output_text.size()) > max_chars) {
                output_text = output_text.substr(0, max_chars);
                result.truncated = true;
            }

            result.output = output_text;
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> WebFetchTool::clone() const {
    return std::make_unique<WebFetchTool>();
}

} // namespace merak::tools
