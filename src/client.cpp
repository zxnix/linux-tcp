#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr char kServerIp[] = "127.0.0.1";
constexpr std::uint16_t kServerPort = 8080;

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

bool receive_exact(int fd, std::string& output, std::size_t expected_size) {
    output.clear();
    output.reserve(expected_size);

    while (output.size() < expected_size) {
        char buffer[1024]{};
        const std::size_t remaining = expected_size - output.size();
        const std::size_t request_size =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        const ssize_t received = ::recv(fd, buffer, request_size, 0);
        if (received > 0) {
            output.append(buffer, static_cast<std::size_t>(received));
            continue;
        }
        if (received == -1 && errno == EINTR) {
            continue;
        }
        if (received == 0) {
            std::cerr << "recv() failed: server closed the connection before "
                         "the complete echo arrived\n";
        } else {
            std::cerr << "recv() failed: " << std::strerror(errno) << '\n';
        }
        return false;
    }

    return true;
}

}  // namespace

int main() {
    const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout << "client socket created, fd = " << client_fd << '\n';

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(kServerPort);

    const int conversion_result =
        ::inet_pton(AF_INET, kServerIp, &server_address.sin_addr);
    if (conversion_result != 1) {
        if (conversion_result == 0) {
            std::cerr << "inet_pton() failed: invalid IPv4 address\n";
        } else {
            std::cerr << "inet_pton() failed: " << std::strerror(errno) << '\n';
        }
        close_fd(client_fd, "client_fd");
        return 1;
    }

    if (::connect(client_fd, reinterpret_cast<const sockaddr*>(&server_address),
                  sizeof(server_address)) == -1) {
        const int connect_errno = errno;
        std::cerr << "connect() returned -1, errno = " << connect_errno << " ("
                  << std::strerror(connect_errno) << ")\n";
        close_fd(client_fd, "client_fd");
        return 1;
    }
    std::cout << "connected to " << kServerIp << ':' << kServerPort << '\n';

    sockaddr_in local_address{};
    socklen_t local_address_size = sizeof(local_address);
    if (::getsockname(client_fd, reinterpret_cast<sockaddr*>(&local_address),
                      &local_address_size) == -1) {
        std::cerr << "getsockname() failed: " << std::strerror(errno) << '\n';
        close_fd(client_fd, "client_fd");
        return 1;
    }

    char local_ip[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &local_address.sin_addr, local_ip,
                    sizeof(local_ip)) == nullptr) {
        std::cerr << "inet_ntop() failed: " << std::strerror(errno) << '\n';
        close_fd(client_fd, "client_fd");
        return 1;
    }

    std::cout << "client local endpoint: " << local_ip << ':'
              << ntohs(local_address.sin_port) << "\n\n";

    bool success = true;
    std::string message;

    while (true) {
        std::cout << "message (quit/exit to close): " << std::flush;
        if (!std::getline(std::cin, message)) {
            std::cout << "\ninput closed\n";
            break;
        }

        if (message == "quit" || message == "exit") {
            break;
        }
        if (message.empty()) {
            std::cout << "empty message not sent\n";
            continue;
        }

        if (!send_all(client_fd, message.data(), message.size())) {
            success = false;
            break;
        }
        std::cout << "sent: " << message << '\n';

        std::string echo;
        if (!receive_exact(client_fd, echo, message.size())) {
            success = false;
            break;
        }
        std::cout << "echo from server: " << echo << '\n';
    }

    const bool closed = close_fd(client_fd, "client_fd");
    std::cout << "\nconnection closed\n";
    return success && closed ? 0 : 1;
}
