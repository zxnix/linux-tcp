#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kServerPort = 8080;
constexpr int kBacklog = 8;
constexpr std::size_t kBufferSize = 1024;
std::mutex output_mutex;

void log_message(const std::string& message) {
    const std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << message << std::flush;
}

void log_error(const std::string& message) {
    const std::lock_guard<std::mutex> lock(output_mutex);
    std::cerr << message << std::flush;
}

bool close_fd(int fd, const char* name) {
    if (::close(fd) == -1) {
        std::ostringstream output;
        output << "close(" << name << ") failed: " << std::strerror(errno)
               << '\n';
        log_error(output.str());
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

        std::ostringstream output;
        output << "send() failed: "
               << (sent == 0 ? "connection made no forward progress"
                             : std::strerror(errno))
               << '\n';
        log_error(output.str());
        return false;
    }

    return true;
}

void handle_client(int conn_fd, std::string client_ip,
                   std::uint16_t client_port) {
    const std::thread::id thread_id = std::this_thread::get_id();
    {
        std::ostringstream output;
        output << "worker started\n"
               << "thread=" << thread_id << '\n'
               << "conn_fd=" << conn_fd << '\n'
               << "client=" << client_ip << ':' << client_port << "\n\n";
        log_message(output.str());
    }

    char buffer[kBufferSize]{};

    while (true) {
        ssize_t received = -1;
        do {
            received = ::recv(conn_fd, buffer, sizeof(buffer), 0);
        } while (received == -1 && errno == EINTR);

        if (received == 0) {
            std::ostringstream output;
            output << "[thread=" << thread_id
                   << "] client closed conn_fd=" << conn_fd << '\n';
            log_message(output.str());
            break;
        }
        if (received == -1) {
            std::ostringstream output;
            output << "recv() failed for conn_fd=" << conn_fd << ": "
                   << std::strerror(errno) << '\n';
            log_error(output.str());
            break;
        }

        const std::string message(buffer, static_cast<std::size_t>(received));
        {
            std::ostringstream output;
            output << "[thread=" << thread_id << "] received on conn_fd="
                   << conn_fd << ": " << message << '\n';
            log_message(output.str());
        }

        if (!send_all(conn_fd, buffer, static_cast<std::size_t>(received))) {
            break;
        }
        {
            std::ostringstream output;
            output << "[thread=" << thread_id << "] echo sent on conn_fd="
                   << conn_fd << '\n';
            log_message(output.str());
        }
    }

    close_fd(conn_fd, "conn_fd");
    {
        std::ostringstream output;
        output << "[thread=" << thread_id << "] connection closed, conn_fd="
               << conn_fd << '\n';
        log_message(output.str());
    }
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

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_size = sizeof(client_address);

        const int conn_fd = ::accept(
            listen_fd, reinterpret_cast<sockaddr*>(&client_address),
            &client_address_size);

        if (conn_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept() failed: " << std::strerror(errno) << '\n';
            continue;
        }

        char client_ip[INET_ADDRSTRLEN]{};
        if (::inet_ntop(AF_INET, &client_address.sin_addr, client_ip,
                        sizeof(client_ip)) == nullptr) {
            std::cerr << "inet_ntop() failed: " << std::strerror(errno)
                      << '\n';
            close_fd(conn_fd, "conn_fd");
            continue;
        }

        const std::uint16_t client_port = ntohs(client_address.sin_port);
        {
            std::ostringstream output;
            output << "\nnew client connected\n"
                   << "client connected from " << client_ip << ':'
                   << client_port << '\n'
                   << "listen_fd = " << listen_fd << '\n'
                   << "conn_fd   = " << conn_fd << "\n\n";
            log_message(output.str());
        }

        try {
            std::thread worker(handle_client, conn_fd, std::string(client_ip),
                               client_port);
            worker.detach();
        } catch (const std::system_error& error) {
            std::ostringstream output;
            output << "failed to create worker thread for conn_fd=" << conn_fd
                   << ": " << error.what() << '\n';
            log_error(output.str());
            close_fd(conn_fd, "conn_fd");
        }
    }
}
