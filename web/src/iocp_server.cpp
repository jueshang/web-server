#include "iocp_server.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

// 构造函数
IocpServer::IocpServer() : 
    running_(false), 
    iocpHandle_(NULL), 
    listenSocket_(INVALID_SOCKET) {}

// 析构函数
IocpServer::~IocpServer() { Stop(); }

// 初始化服务器
bool IocpServer::Initialize(int port) {
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // 创建监听套接字
    if (!CreateListenSocket(port)) {
        WSACleanup();
        return false;
    }

    // 设置完成端口
    if (!SetupCompletionPort()) {
        closesocket(listenSocket_);
        WSACleanup();
        return false;
    }

    // 创建工作线程
    CreateWorkerThreads();

    // 创建文档根目录
    if (!fs::exists(documentRoot_)) {
        if (!fs::create_directory(documentRoot_)) {
            std::cerr << "Failed to create document root directory" << std::endl;
            closesocket(listenSocket_);
            WSACleanup();
            return false;
        }
    }

    // 初始化定时器
    timer_ = std::make_unique<TimerWheel>();
    timer_->Start();

    running_ = true;
    StartAccept();  // 开始接受连接
    
    std::cout << "Server initialized successfully. Listening on port " << port << std::endl;
    return true;
}

// 创建监听套接字
bool IocpServer::CreateListenSocket(int port) {
    // 创建支持重叠I/O的套接字
    listenSocket_ = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET) {
        std::cerr << "WSASocket failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // 设置套接字选项
    int reuse = 1;
    if (setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, 
                  (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << WSAGetLastError() << std::endl;
    }

    // 禁用Nagle算法
    int nodelay = 1;
    if (setsockopt(listenSocket_, IPPROTO_TCP, TCP_NODELAY,
                  (const char*)&nodelay, sizeof(nodelay)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(TCP_NODELAY) failed: " << WSAGetLastError() << std::endl;
    }

    // 设置接收缓冲区大小
    int recvBufSize = 64 * 1024; // 64KB
    if (setsockopt(listenSocket_, SOL_SOCKET, SO_RCVBUF,
                  (const char*)&recvBufSize, sizeof(recvBufSize)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(SO_RCVBUF) failed: " << WSAGetLastError() << std::endl;
    }

    // 绑定套接字
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket_);
        return false;
    }

    // 开始监听
    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket_);
        return false;
    }

    return true;
}

// 设置完成端口
bool IocpServer::SetupCompletionPort() {
    // 创建IOCP内核对象
    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocpHandle_ == NULL) {
        std::cerr << "CreateIoCompletionPort failed: " << GetLastError() << std::endl;
        return false;
    }

    // 关联监听套接字
    if (CreateIoCompletionPort((HANDLE)listenSocket_, iocpHandle_, (ULONG_PTR)listenSocket_, 0) == NULL) {
        std::cerr << "Associating listen socket failed: " << GetLastError() << std::endl;
        CloseHandle(iocpHandle_);
        return false;
    }

    return true;
}

// 创建工作线程
void IocpServer::CreateWorkerThreads() {
    // 根据CPU核心数创建线程
    int threadCount = GetDefaultThreadCount();
    workerThreads_.reserve(threadCount);
    
    for (int i = 0; i < threadCount; ++i) {
        workerThreads_.emplace_back([this]() {
            DWORD bytesTransferred;
            ULONG_PTR completionKey;
            LPOVERLAPPED overlapped;
            
            while (running_) {
                // 从完成端口获取I/O操作结果
                BOOL result = GetQueuedCompletionStatus(
                    iocpHandle_,
                    &bytesTransferred,
                    &completionKey,
                    &overlapped,
                    INFINITE);
                
                if (!running_) break;
                
                // 处理I/O错误
                if (!result) {
                    DWORD error = GetLastError();
                    switch (error) {
                        case ERROR_NETNAME_DELETED:    // 正常连接断开
                        case ERROR_CONNECTION_ABORTED: // 连接中止
                            break;
                        default:
                            std::cerr << "IOCP Error (code " << error << "): ";
                            PrintWindowsError(error);
                    }
                    
                    // 清理资源
                    if (overlapped) {
                        PerIoData* perIoData = CONTAINING_RECORD(overlapped, PerIoData, overlapped);
                        CloseClientSocket(perIoData->socket);
                        delete perIoData;
                    }
                    continue;
                }
                
                // 检查重叠结构是否有效
                if (!overlapped) {
                    std::cerr << "Warning: Null OVERLAPPED received" << std::endl;
                    continue;
                }
                
                // 处理完成的I/O操作
                PerIoData* perIoData = CONTAINING_RECORD(overlapped, PerIoData, overlapped);
                HandleIoCompletion(bytesTransferred, perIoData);
            }
        });
    }
}

// 开始接受连接
void IocpServer::StartAccept() {
    // 创建客户端套接字
    SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Accept socket failed: " << WSAGetLastError() << std::endl;
        return;
    }

    // 创建Accept专用的I/O数据结构
    PerIoData* acceptData = new PerIoData(clientSocket, IoOperation::ACCEPT);
    
    // 发起异步AcceptEx操作
    if (AcceptEx(listenSocket_, clientSocket, acceptData->buffer, 0,
                sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
                NULL, &acceptData->overlapped) == FALSE) {
        DWORD error = WSAGetLastError();
        if (error != ERROR_IO_PENDING) {
            std::cerr << "AcceptEx failed: " << error << std::endl;
            closesocket(clientSocket);
            delete acceptData;
        }
    }
}

// 处理I/O完成
void IocpServer::HandleIoCompletion(DWORD bytesTransferred, PerIoData* perIoData) {
    std::unique_ptr<PerIoData> guard(perIoData); // 自动管理生命周期
    
    switch (perIoData->operation) {
        case IoOperation::ACCEPT:
            HandleAccept(perIoData);
            guard.release(); // HandleAccept会接管所有权
            break;
        case IoOperation::RECV:
            HandleRecv(perIoData, bytesTransferred);
            guard.release(); // HandleRecv会删除或重用
            break;
        case IoOperation::SEND:
            HandleSend(perIoData);
            guard.release(); // HandleSend会删除或重用
            break;
    }
}

// 处理接受连接
void IocpServer::HandleAccept(PerIoData* acceptData) {
    SOCKET clientSocket = acceptData->socket;

    // 设置TCP_NODELAY选项
    int opt = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

    // 将客户端套接字与IOCP关联
    if (CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle_, (ULONG_PTR)clientSocket, 0) == NULL) {
        std::cerr << "Failed to associate client socket: " << GetLastError() << std::endl;
        closesocket(clientSocket);
        delete acceptData;
        return;
    }

    // 添加到客户端列表
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_[clientSocket] = ClientContext();
    }

    // 设置超时定时器
    {
        std::lock_guard<std::mutex> lock(timeoutMutex_);
        timeout_ids_[clientSocket] = timer_->AddTimeout(120s, [this, clientSocket]() {
            CloseClientSocket(clientSocket);
        });
    }

    // 开始接收数据
    PerIoData* newRecvData = new PerIoData(clientSocket, IoOperation::RECV);
    PostRecv(newRecvData);

    // 继续接受新连接
    StartAccept();
    delete acceptData;
}

// 处理接收数据
void IocpServer::HandleRecv(PerIoData* recvData, DWORD bytesTransferred) {
    SOCKET clientSocket = recvData->socket;
    
    if (bytesTransferred == 0) {
        CloseClientSocket(clientSocket);
        delete recvData;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto& client = clients_[clientSocket];
        
        client.partial_request.append(recvData->buffer, bytesTransferred);
        
        HttpParser parser;
        if (parser.parse(client.partial_request.c_str(), 
                       client.partial_request.size()) == ParseStatus::SUCCESS) {
            const auto& request = parser.request();
            
            // 处理请求
            ProcessHttpRequest(clientSocket, request);
            
            client.partial_request.clear();
        }
        
        // 重用或创建新的接收缓冲区
        if (!client.pending_recv) {
            client.pending_recv = std::make_unique<PerIoData>(clientSocket, IoOperation::RECV);
        }
        PostRecv(client.pending_recv.get());
    }
    
    delete recvData;
}

// 处理HTTP请求
void IocpServer::ProcessHttpRequest(SOCKET clientSocket, const HttpRequest& request) {
    std::string path = request.uri;
    if (path == "/") path = "/index.html";

    // 安全检查
    if (path.find("..") != std::string::npos) {
        std::string response = BuildHttpResponse("Invalid path", "text/plain", 400);
        PostSend(new PerIoData(clientSocket, IoOperation::SEND, response));
        return;
    }

    // 构建文件路径
    fs::path filePath = fs::path(documentRoot_) / path.substr(1);

    // 检查文件是否存在
    if (!fs::exists(filePath)) {
        std::string response = BuildHttpResponse("File not found", "text/plain", 404);
        PostSend(new PerIoData(clientSocket, IoOperation::SEND, response));
        return;
    }

    // 读取文件内容
    std::ifstream file(filePath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), 
                       std::istreambuf_iterator<char>());

    // 确定Content-Type
    std::string contentType = "text/plain";
    if (filePath.extension() == ".html") contentType = "text/html";
    else if (filePath.extension() == ".css") contentType = "text/css";
    else if (filePath.extension() == ".js") contentType = "application/javascript";
    else if (filePath.extension() == ".jpg" || filePath.extension() == ".jpeg") contentType = "image/jpeg";
    else if (filePath.extension() == ".png") contentType = "image/png";

    // 构建并发送响应
    std::string response = BuildHttpResponse(content, contentType);
    PostSend(new PerIoData(clientSocket, IoOperation::SEND, response));
}

// 处理图片上传
void IocpServer::ProcessImageUpload(SOCKET clientSocket, const std::string& imageData) {
    // 实际使用imageData参数
    (void)imageData; // 避免未使用警告
    
    std::string response = BuildHttpResponse(
        "Image processed successfully", 
        "text/plain", 
        200
    );
    
    PostSend(new PerIoData(clientSocket, IoOperation::SEND, response));
}

// 构建HTTP响应
std::string IocpServer::BuildHttpResponse(
    const std::string& content, 
    const std::string& contentType, 
    int statusCode) 
{
    static const std::unordered_map<int, std::string> statusTexts = {
        {200, "OK"},
        {400, "Bad Request"},
        {404, "Not Found"},
        {500, "Internal Server Error"}
    };

    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusTexts.at(statusCode) << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << content.size() << "\r\n"
        << "Connection: keep-alive\r\n\r\n"
        << content;

    return oss.str();
}

// 投递接收操作
void IocpServer::PostRecv(PerIoData* perIoData) {
    DWORD flags = 0;
    DWORD bytesRecv = 0;
    
    ZeroMemory(&perIoData->overlapped, sizeof(OVERLAPPED));
    ZeroMemory(perIoData->buffer, sizeof(perIoData->buffer));
    perIoData->wsaBuf.buf = perIoData->buffer;
    perIoData->wsaBuf.len = sizeof(perIoData->buffer);

    int timeout = 30000;
    setsockopt(perIoData->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (WSARecv(
        perIoData->socket,
        &perIoData->wsaBuf,
        1,
        &bytesRecv,
        &flags,
        &perIoData->overlapped,
        NULL
    ) == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            std::cerr << "WSARecv error: " << err << std::endl;
            CloseClientSocket(perIoData->socket);
            delete perIoData;
        }
    }
}

// 投递发送操作
void IocpServer::PostSend(PerIoData* perIoData) {
    DWORD bytesSent = 0;
    
    // 发起异步发送操作
    if (WSASend(
        perIoData->socket, 
        &perIoData->wsaBuf, 
        1, 
        &bytesSent, 
        0, 
        &perIoData->overlapped, 
        NULL
    ) == SOCKET_ERROR) {
        DWORD error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cerr << "WSASend failed: " << error << std::endl;
            CloseClientSocket(perIoData->socket);
            delete perIoData;
        }
    }
}

// 处理发送完成
void IocpServer::HandleSend(PerIoData* sendData) {
    // 发送完成后准备接收下一个请求
    PerIoData* recvData = new PerIoData(sendData->socket, IoOperation::RECV);
    PostRecv(recvData);
    delete sendData;
}

// 关闭客户端套接字
void IocpServer::CloseClientSocket(SOCKET socket) {
    if (socket != INVALID_SOCKET) {
        // 从客户端列表中移除
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_.erase(socket);
        }
        
        // 取消定时器
        {
            std::lock_guard<std::mutex> lock(timeoutMutex_);
            if (timeout_ids_.count(socket)) {
                timer_->CancelTimeout(timeout_ids_[socket]);
                timeout_ids_.erase(socket);
            }
        }
        
        // 关闭套接字
        closesocket(socket);
    }
}

// 运行服务器
void IocpServer::Run() {
    std::cout << "Server running on port " << DEFAULT_PORT << std::endl;
    std::cout << "Worker threads: " << workerThreads_.size() << std::endl;
    std::cout << "Document root: " << documentRoot_ << std::endl;
    
    // 主循环(实际工作由工作线程完成)
    while (running_) {
        std::this_thread::sleep_for(1s);
    }
}

// 停止服务器
void IocpServer::Stop() {
    if (!running_) return;
    
    // 1. 设置运行标志为false
    running_ = false;
    
    // 2. 先停止定时器
    if (timer_) {
        timer_->Stop();
    }
    
    // 3. 通知所有工作线程退出
    for (size_t i = 0; i < workerThreads_.size(); ++i) {
        PostQueuedCompletionStatus(iocpHandle_, 0, 0, NULL);
    }
    
    // 4. 等待所有工作线程结束
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) thread.join();
    }
    
    // 5. 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& client : clients_) {
            closesocket(client.first);
        }
        clients_.clear();
    }
    
    // 6. 关闭监听套接字
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    
    // 7. 关闭IOCP句柄
    if (iocpHandle_ != NULL) {
        CloseHandle(iocpHandle_);
        iocpHandle_ = NULL;
    }
    
    // 8. 清理Winsock
    WSACleanup();
    
    std::cout << "Server stopped successfully" << std::endl;
}