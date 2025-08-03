#include "iocp_server.hpp"
#include <iostream>
#include <csignal>

std::unique_ptr<IocpServer> server;  // 服务器实例

// 信号处理函数
void SignalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nShutting down server..." << std::endl;
        if (server) server->Stop();  // 停止服务器
    }
}

// 主函数
int main() {
    signal(SIGINT, SignalHandler);  // 设置信号处理
    
    try {
        std::cout << "Starting IOCP Web Server..." << std::endl;
        server = std::make_unique<IocpServer>();  // 创建服务器实例
        
        // 初始化服务器
        if (!server->Initialize()) {
            std::cerr << "Initialization failed" << std::endl;
            return 1;
        }
        
        server->Run();  // 运行服务器
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}