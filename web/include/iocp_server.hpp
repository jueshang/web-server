#ifndef IOCP_SERVER_HPP
#define IOCP_SERVER_HPP

#include "common.hpp"
#include "timer.hpp"
#include "http_parser.hpp"
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <filesystem>

// IOCP服务器类
class IocpServer {
public:
    IocpServer();
    ~IocpServer();

    bool Initialize(int port = DEFAULT_PORT);  // 初始化服务器
    void Run();      // 运行服务器
    void Stop();     // 停止服务器

private:
    // 私有方法
    bool CreateListenSocket(int port);  // 创建监听套接字
    bool SetupCompletionPort();         // 设置完成端口
    void CreateWorkerThreads();         // 创建工作线程
    void StartAccept();                 // 开始接受连接
    void HandleIoCompletion(DWORD bytesTransferred, PerIoData* perIoData);  // 处理I/O完成
    void HandleAccept(PerIoData* acceptData);    // 处理接受连接
    void HandleRecv(PerIoData* recvData, DWORD bytesTransferred);  // 处理接收数据
    void HandleSend(PerIoData* sendData);        // 处理发送数据
    void ProcessHttpRequest(SOCKET clientSocket, const HttpRequest& request);  // 处理HTTP请求
    void ProcessImageUpload(SOCKET clientSocket, const std::string& imageData);  // 处理图片上传
    std::string BuildHttpResponse(const std::string& content,  // 构建HTTP响应
                                 const std::string& contentType, 
                                 int statusCode = 200);
    void PostRecv(PerIoData* perIoData);  // 投递接收操作
    void PostSend(PerIoData* perIoData);  // 投递发送操作
    void CloseClientSocket(SOCKET socket);  // 关闭客户端套接字

    // 客户端上下文结构
    struct ClientContext {
        std::string partial_request;  // 部分请求数据
        std::unique_ptr<PerIoData> pending_recv;  // 待处理的接收操作
    };

    // 成员变量
    std::atomic<bool> running_;        // 服务器运行标志
    HANDLE iocpHandle_;               // IOCP句柄
    SOCKET listenSocket_;             // 监听套接字
    std::vector<std::thread> workerThreads_;  // 工作线程
    std::unordered_map<SOCKET, ClientContext> clients_;  // 客户端映射
    std::mutex clientsMutex_;         // 客户端映射互斥锁
    std::string documentRoot_ = "./www";  // 文档根目录
    std::unique_ptr<TimerWheel> timer_;  // 定时器轮
    std::unordered_map<SOCKET, uint64_t> timeout_ids_;  // 超时ID映射
    std::mutex timeoutMutex_;         // 超时映射互斥锁
};

#endif