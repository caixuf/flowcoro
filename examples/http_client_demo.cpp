// App-facing HTTP client demo.
// Default: hits a built-in local stub (no external network).
// Optional: ./http_client_demo http://host:port/path
// Does not touch core scheduler / thread_pool.

#include "flowcoro.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace flowcoro;
using namespace flowcoro::net;

#ifndef _WIN32
namespace {

class LocalHttpStub {
public:
    explicit LocalHttpStub(std::string body)
        : body_(std::move(body)), running_(true) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) throw std::runtime_error("socket failed");
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listen_fd_, 8) != 0) {
            ::close(listen_fd_);
            throw std::runtime_error("bind/listen failed");
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        worker_ = std::thread([this] {
            while (running_.load()) {
                int client = ::accept(listen_fd_, nullptr, nullptr);
                if (client < 0) continue;
                if (!running_.load()) { ::close(client); break; }
                char buf[4096];
                (void)::recv(client, buf, sizeof(buf), 0);
                std::string resp =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                    "Content-Length: " + std::to_string(body_.size()) +
                    "\r\nConnection: close\r\n\r\n" + body_;
                (void)::send(client, resp.data(), resp.size(), 0);
                ::close(client);
            }
        });
    }
    ~LocalHttpStub() {
        running_.store(false);
        int poke = ::socket(AF_INET, SOCK_STREAM, 0);
        if (poke >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_);
            ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            ::close(poke);
        }
        if (worker_.joinable()) worker_.join();
        if (listen_fd_ >= 0) ::close(listen_fd_);
    }
    uint16_t port() const { return port_; }
private:
    std::string body_;
    std::atomic<bool> running_;
    int listen_fd_{-1};
    uint16_t port_{0};
    std::thread worker_;
};

}  // namespace
#endif

int main(int argc, char** argv) {
    HttpClient client;
    std::string url;
#ifndef _WIN32
    std::unique_ptr<LocalHttpStub> stub;
    if (argc >= 2) {
        url = argv[1];
    } else {
        stub = std::make_unique<LocalHttpStub>("hello from flowcoro http scaffold");
        url = "http://127.0.0.1:" + std::to_string(stub->port()) + "/demo";
        std::cout << "Local stub on " << url << "\n";
    }
#else
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " http://host:port/path\n";
        return 2;
    }
    url = argv[1];
#endif

    auto response = sync_wait(client.get(url));
    std::cout << "GET " << url << "\n"
              << "  success=" << (response.success ? "true" : "false") << "\n"
              << "  status=" << response.status_code << " " << response.status_text << "\n"
              << "  body=" << response.body << "\n";
    if (!response.error_message.empty()) {
        std::cout << "  error=" << response.error_message << "\n";
    }
    return response.success ? 0 : 1;
}
