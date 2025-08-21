#include <iostream>

int main() {
    std::cout << "FlowCoro 聊天客户端" << std::endl;
    std::cout << "使用以下命令连接聊天服务器:" << std::endl;
    std::cout << "  telnet localhost 8080" << std::endl;
    std::cout << "  nc localhost 8080" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "连接后直接输入消息，其他客户端会收到。" << std::endl;
    return 0;
}
