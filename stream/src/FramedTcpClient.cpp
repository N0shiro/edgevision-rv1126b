#include "FramedTcpClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

FramedTcpClient::FramedTcpClient(std::string server_ip, int server_port)
    : server_ip_(std::move(server_ip)),
      server_port_(server_port),
      sock_(-1) {}

FramedTcpClient::~FramedTcpClient() {
    closeConnection();
}

bool FramedTcpClient::connectOnce() {
    closeConnection();

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) {
        std::cerr << "创建推流 socket 失败: " << std::strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(server_port_));

    if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "推流目标 IP 无效: " << server_ip_ << std::endl;
        closeConnection();
        return false;
    }

    if (connect(sock_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "连接推流网关失败: " << std::strerror(errno) << std::endl;
        closeConnection();
        return false;
    }

    std::cout << "已连接推流网关 " << server_ip_ << ':' << server_port_ << std::endl;
    return true;
}

bool FramedTcpClient::sendPacket(const std::vector<uint8_t>& packet) {
    if (sock_ < 0) {
        return false;
    }

    const uint32_t payload_size = static_cast<uint32_t>(packet.size());
    const uint32_t net_payload_size = htonl(payload_size);

    if (!sendAll(reinterpret_cast<const uint8_t*>(&net_payload_size), sizeof(net_payload_size))) {
        return false;
    }

    return sendAll(packet.data(), packet.size());
}

void FramedTcpClient::closeConnection() {
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

bool FramedTcpClient::isConnected() const {
    return sock_ >= 0;
}

bool FramedTcpClient::sendAll(const uint8_t* data, size_t size) {
    size_t total_sent = 0;
    while (total_sent < size) {
        const ssize_t sent = send(
            sock_,
            data + total_sent,
            size - total_sent,
            MSG_NOSIGNAL
        );

        if (sent > 0) {
            total_sent += static_cast<size_t>(sent);
            continue;
        }

        if (sent < 0 && errno == EINTR) {
            continue;
        }

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(1000);
            continue;
        }

        std::cerr << "发送 H.264 数据失败: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}
