#include <iostream>
#include <cstdlib>
#include "TcpServer.h"

namespace {

int readGatewayPort() {
    const char* raw_port = std::getenv("AICAM_GATEWAY_PORT");
    if (raw_port == nullptr || *raw_port == '\0') {
        return 8080;
    }

    const int port = std::atoi(raw_port);
    if (port <= 0 || port > 65535) {
        std::cerr << "AICAM_GATEWAY_PORT 非法，回退到 8080: " << raw_port << std::endl;
        return 8080;
    }

    return port;
}

}  // namespace

int main() {
    const int port = readGatewayPort();
    std::cout << "网关启动，监听端口: " << port << std::endl;
    TcpServer server(port);
    server.start();
    return 0;
}
