#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kServerPort = 8080;
constexpr int kBacklog = 8;
constexpr std::size_t kBufferSize = 1024;

bool close_fd(int fd, const char* name) {
    if (::close(fd) == -1) {
        std::cerr << "close(" << name << ") failed: " << std::strerror(errno)
                  << '\n';
        return false;
    }
    return true;
}

bool send_all(int fd, const char* data, std::size_t size) {
    std::size_t sent_total = 0;

    while (sent_total < size) {
        const ssize_t sent =
            ::send(fd, data + sent_total, size - sent_total, MSG_NOSIGNAL);
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == -1 && errno == EINTR) {
            continue;
        }

        std::cerr << "send() failed: "
                  << (sent == 0 ? "connection made no forward progress"
                                : std::strerror(errno))
                  << '\n';
        return false;
    }

    return true;
}

}  // namespace

int main() {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "server socket created, fd = " << listen_fd << '\n';

    int reuse_address = 1;
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                     sizeof(reuse_address)) == -1) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: "
                  << std::strerror(errno) << '\n';
        close_fd(listen_fd, "listen_fd");
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(kServerPort);

    if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&server_address),
               sizeof(server_address)) == -1) {
        std::cerr << "bind() failed: " << std::strerror(errno) << '\n';
        close_fd(listen_fd, "listen_fd");
        return 1;
    }
    std::cout << "server bound to 0.0.0.0:" << kServerPort << '\n';

    if (::listen(listen_fd, kBacklog) == -1) {
        std::cerr << "listen() failed: " << std::strerror(errno) << '\n';
        close_fd(listen_fd, "listen_fd");
        return 1;
    }
    std::cout << "server listening on 0.0.0.0:" << kServerPort << '\n';
    std::cout << "waiting for client..." << std::endl;

    sockaddr_in client_address{};
    socklen_t client_address_size = sizeof(client_address);
    int conn_fd = -1;

    do {
        conn_fd = ::accept(
            listen_fd, reinterpret_cast<sockaddr*>(&client_address),
            &client_address_size);
    } while (conn_fd == -1 && errno == EINTR);

    if (conn_fd == -1) {
        std::cerr << "accept() failed: " << std::strerror(errno) << '\n';
        close_fd(listen_fd, "listen_fd");
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &client_address.sin_addr, client_ip,
                    sizeof(client_ip)) == nullptr) {
        std::cerr << "inet_ntop() failed: " << std::strerror(errno) << '\n';
        close_fd(conn_fd, "conn_fd");
        close_fd(listen_fd, "listen_fd");
        return 1;
    }

    std::cout << '\n';
    std::cout << "new client connected\n";
    std::cout << "client connected from " << client_ip << ':'
              << ntohs(client_address.sin_port) << '\n';
    std::cout << "listen_fd = " << listen_fd << '\n';
    std::cout << "conn_fd   = " << conn_fd << "\n\n";

    char buffer[kBufferSize]{};
    ssize_t received = -1;
    do {
        received = ::recv(conn_fd, buffer, sizeof(buffer), 0);
    } while (received == -1 && errno == EINTR);

    bool success = true;
    if (received == -1) {
        std::cerr << "recv() failed: " << std::strerror(errno) << '\n';
        success = false;
    } else if (received == 0) {
        std::cout << "client closed the connection without sending data\n";
    } else {
        const std::string message(buffer, static_cast<std::size_t>(received));
        std::cout << "received: " << message << '\n';

        if (!send_all(conn_fd, buffer, static_cast<std::size_t>(received))) {
            success = false;
        } else {
            std::cout << "echo sent\n";
        }
    }

    const bool conn_closed = close_fd(conn_fd, "conn_fd");
    const bool listen_closed = close_fd(listen_fd, "listen_fd");
    success = success && conn_closed && listen_closed;

    std::cout << "\nconnection closed\n";
    return success ? 0 : 1;
}
