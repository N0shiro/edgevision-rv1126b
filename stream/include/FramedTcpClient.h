#pragma once

#include <cstdint>
#include <string>
#include <vector>

class FramedTcpClient {
public:
    FramedTcpClient(std::string server_ip, int server_port);
    ~FramedTcpClient();

    bool connectOnce();
    bool sendPacket(const std::vector<uint8_t>& packet);
    void closeConnection();
    bool isConnected() const;

private:
    bool sendAll(const uint8_t* data, size_t size);

    std::string server_ip_;
    int server_port_;
    int sock_;
};
