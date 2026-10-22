/**
 * @file http_server.h
 * @brief Thin HttpServer façade over net::TcpServer for app-facing demos.
 *
 * Header-only. Does not touch the scheduler or lockfree core.
 * Include via <flowcoro.hpp> or this path.
 */
#pragma once

#include "http_client.h"
#include "net.h"

#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace flowcoro::net {

/** Incoming HTTP/1.1 request parsed from a TcpConnection. */
struct HttpIncomingRequest {
    HttpMethod method = HttpMethod::GET;
    std::string target;  // raw request-target (path[?query])
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

using HttpRequestHandler =
    std::function<Task<HttpResponse>(const HttpIncomingRequest&)>;

inline HttpMethod parse_http_method(const std::string& m) {
    if (m == "GET") return HttpMethod::GET;
    if (m == "POST") return HttpMethod::POST;
    if (m == "PUT") return HttpMethod::PUT;
    if (m == "DELETE") return HttpMethod::DELETE;
    if (m == "PATCH") return HttpMethod::PATCH;
    if (m == "HEAD") return HttpMethod::HEAD;
    if (m == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::GET;
}

inline const char* http_method_cstr(HttpMethod m) {
    switch (m) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
    }
    return "GET";
}

inline std::string http_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

inline std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::string trim_ws(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

/**
 * Minimal HTTP/1.1 server: route table + TcpServer accept loop.
 * One request per connection (Connection: close). Enough for demos/tests.
 */
class HttpServer {
public:
    explicit HttpServer(EventLoop* loop) : tcp_(loop) {
        tcp_.set_connection_handler(
            [this](std::unique_ptr<Socket> sock) -> Task<void> {
                co_await handle_connection(std::move(sock));
            });
    }

    ~HttpServer() { stop(); }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void route(HttpMethod method, const std::string& path, HttpRequestHandler handler) {
        routes_[route_key(method, path)] = std::move(handler);
    }

    void get(const std::string& path, HttpRequestHandler handler) {
        route(HttpMethod::GET, path, std::move(handler));
    }

    void post(const std::string& path, HttpRequestHandler handler) {
        route(HttpMethod::POST, path, std::move(handler));
    }

    void set_default_handler(HttpRequestHandler handler) {
        default_handler_ = std::move(handler);
    }

    Task<void> listen(const std::string& host, uint16_t port) {
        co_await tcp_.listen(host, port);
        co_return;
    }

    void stop() { tcp_.stop(); }

    bool is_running() const { return tcp_.is_running(); }

    uint16_t local_port() const { return tcp_.local_port(); }

private:
    static std::string route_key(HttpMethod method, const std::string& path) {
        return std::string(http_method_cstr(method)) + " " + path;
    }

    Task<HttpIncomingRequest> read_request(TcpConnection& conn) {
        HttpIncomingRequest req;
        std::string first = co_await conn.read_line();
        if (first.empty()) {
            co_return req;
        }
        while (!first.empty() && (first.back() == '\r' || first.back() == '\n')) {
            first.pop_back();
        }

        {
            std::istringstream iss(first);
            std::string method_s, version;
            iss >> method_s >> req.target >> version;
            req.method = parse_http_method(method_s);
            auto qpos = req.target.find('?');
            if (qpos == std::string::npos) {
                req.path = req.target.empty() ? "/" : req.target;
            } else {
                req.path = req.target.substr(0, qpos);
                req.query = req.target.substr(qpos + 1);
            }
            if (req.path.empty()) req.path = "/";
        }

        size_t content_length = 0;
        while (true) {
            std::string line = co_await conn.read_line();
            if (line.empty()) break;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            if (line.empty()) break;

            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = trim_ws(line.substr(0, colon));
            std::string value = trim_ws(line.substr(colon + 1));
            if (to_lower_ascii(key) == "content-length") {
                try {
                    content_length = static_cast<size_t>(std::stoull(value));
                } catch (...) {
                    content_length = 0;
                }
            }
            req.headers[key] = value;
        }

        if (content_length > 0) {
            req.body = co_await conn.read(content_length);
        }
        co_return req;
    }

    static std::string serialize_response(const HttpResponse& resp) {
        int code = resp.status_code > 0 ? resp.status_code : 200;
        std::string text = resp.status_text.empty() ? http_status_text(code)
                                                    : resp.status_text;
        std::ostringstream out;
        out << "HTTP/1.1 " << code << " " << text << "\r\n";
        bool has_len = false;
        bool has_conn = false;
        for (const auto& [k, v] : resp.headers) {
            out << k << ": " << v << "\r\n";
            auto lk = to_lower_ascii(k);
            if (lk == "content-length") has_len = true;
            if (lk == "connection") has_conn = true;
        }
        if (!has_len) {
            out << "Content-Length: " << resp.body.size() << "\r\n";
        }
        if (!has_conn) {
            out << "Connection: close\r\n";
        }
        out << "\r\n";
        out << resp.body;
        return out.str();
    }

    Task<void> handle_connection(std::unique_ptr<Socket> sock) {
        try {
            TcpConnection conn(std::move(sock));
            auto req = co_await read_request(conn);
            if (req.target.empty() && req.path.empty()) {
                conn.close();
                co_return;
            }

            HttpResponse resp;
            auto key = route_key(req.method, req.path);
            auto it = routes_.find(key);
            if (it != routes_.end()) {
                resp = co_await it->second(req);
            } else if (default_handler_) {
                resp = co_await default_handler_(req);
            } else {
                resp.status_code = 404;
                resp.status_text = "Not Found";
                resp.body = "Not Found";
                resp.success = false;
            }

            co_await conn.write(serialize_response(resp));
            co_await conn.flush();
            conn.close();
        } catch (...) {
        }
        co_return;
    }

    TcpServer tcp_;
    std::unordered_map<std::string, HttpRequestHandler> routes_;
    HttpRequestHandler default_handler_;
};

}  // namespace flowcoro::net
