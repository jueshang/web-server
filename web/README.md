# 基于IOCP的高性能Web服务器

## 项目背景

在当今互联网应用中，高并发处理能力是Web服务器的核心需求。传统的同步I/O模型（如Apache的prefork模式）在面对大量并发连接时，会因线程/进程切换开销导致性能急剧下降。Windows平台下的IOCP（I/O Completion Ports）模型是微软提供的一种高效异步I/O解决方案，特别适合开发高并发网络服务器。

本项目旨在实现一个基于IOCP模型的高性能Web服务器，解决以下关键问题：

- 高并发连接下的资源利用率问题
- 传统同步I/O模型的性能瓶颈
- Windows平台下高效网络编程的复杂性

## 设计思路

### 2.1 架构设计

采用"事件驱动+线程池"的混合模型：

1. **IOCP核心**：作为事件通知中心，管理所有I/O操作完成事件
2. **工作线程池**：处理实际业务逻辑，线程数根据CPU核心动态调整
3. **定时器轮**：高效管理连接超时等定时任务
4. **协议解析器**：专门处理HTTP协议解析

### 2.2 关键设计决策

1. **异步I/O模型选择**：
   - 比较了select/poll/epoll/IOCP等模型
   - 选择IOCP因其在Windows平台最优性能
   - 支持真正的异步非阻塞I/O
2. **线程模型**：
   - 避免"一个连接一个线程"的传统模式
   - 采用固定大小线程池+事件驱动
   - 工作线程数 = CPU核心数 × 2
3. **性能优化**：
   - 零拷贝技术减少内存复制
   - 批处理I/O操作
   - 对象池重用关键数据结构

## 核心实现详解

### 1. IOCP服务器初始化（iocp_server.cpp）

```c++
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

    running_ = true;
    StartAccept();  // 开始接受连接
    
    std::cout << "Server initialized successfully. Listening on port " << port << std::endl;
    return true;
}
```

1. **Winsock初始化**：
   - 使用`WSAStartup`初始化Winsock 2.2版本
   - 失败时输出错误信息并返回false
2. **监听套接字创建**：
   - 调用`CreateListenSocket`方法创建绑定端口的套接字
   - 失败时清理Winsock资源
3. **完成端口设置**：
   - 调用`SetupCompletionPort`创建IOCP内核对象
   - 失败时关闭套接字并清理资源
4. **线程池创建**：
   - `CreateWorkerThreads`根据CPU核心数创建工作线程
   - 线程数默认为核心数×2
5. **启动流程**：
   - 设置运行标志`running_`为true
   - 调用`StartAccept`开始接受客户端连接
   - 输出成功启动信息

### 2. 定时器轮实现（timer.cpp）

```c++
void TimerWheel::RunLoop() {
    using clock = std::chrono::steady_clock;
    auto next_tick = clock::now() + interval_;
    
    while (running_) {
        std::vector<TimerTask> tasks_to_run;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& tasks = wheel_[current_slot_];
            
            // 移动所有任务到执行列表
            for (auto it = tasks.begin(); it != tasks.end();) {
                tasks_to_run.push_back(std::move(it->second));
                it = tasks.erase(it);
            }
            
            current_slot_ = (current_slot_ + 1) % slots_;
        }
        
        // 执行所有到期任务
        for (auto& task : tasks_to_run) {
            try {
                task.callback();
            } catch (...) {}
        }
        
        // 等待到下一个tick
        std::this_thread::sleep_until(next_tick);
        next_tick += interval_;
    }
}
```

1. **时间控制**：
   - 使用`steady_clock`保证稳定的时间间隔
   - 计算下次触发时间`next_tick`
2. **任务处理**：
   - 加锁保护共享数据`wheel_`
   - 移动当前槽位的所有任务到临时列表
   - 更新当前槽位指针
3. **任务执行**：
   - 遍历执行所有到期任务
   - 捕获回调函数中的异常防止崩溃
4. **定时控制**：
   - 使用`sleep_until`精确等待到下一个tick
   - 更新下次触发时间
5. **线程安全**：
   - 使用互斥锁保护任务队列
   - 任务移动操作在锁范围内完成

### 3. HTTP解析器实现（http_parser.cpp）

```c++
ParseStatus HttpParser::parse(const char* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        char c = data[i];
        
        switch (state_) {
            case State::METHOD:
                if (c == ' ') {
                    if (current_header_ == "GET") {
                        request_.method = HttpMethod::GET;
                    } else if (current_header_ == "POST") {
                        request_.method = HttpMethod::POST;
                    } else {
                        return ParseStatus::FAILED;
                    }
                    current_header_.clear();
                    state_ = State::URI;
                } else {
                    current_header_ += c;
                }
                break;
                
            // 其他状态处理...
        }
    }
    return (state_ == State::COMPLETE) ? ParseStatus::SUCCESS : ParseStatus::INCOMPLETE;
}
```

1. **状态机设计**：
   - 使用枚举`State`表示当前解析状态
   - 包括METHOD、URI、VERSION等状态
2. **方法解析**：
   - 收集字符直到遇到空格
   - 判断是GET还是POST方法
   - 非法方法返回FAILED
3. **增量解析**：
   - 逐个字符处理输入数据
   - 支持部分数据解析
   - 返回INCOMPLETE表示需要更多数据
4. **结果返回**：
   - 完成解析返回SUCCESS
   - 否则返回INCOMPLETE
5. **错误处理**：
   - 非法方法立即返回失败
   - 不完整数据可继续接收

### 4. 客户端连接处理（iocp_server.cpp）

```c++
void IocpServer::HandleAccept(PerIoData* acceptData) {
    SOCKET clientSocket = acceptData->socket;

    // 设置TCP_NODELAY选项
    int opt = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));

    // 关联客户端套接字到IOCP
    if (CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle_, (ULONG_PTR)clientSocket, 0) == NULL) {
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
```

1. **套接字设置**：
   - 启用TCP_NODELAY禁用Nagle算法
   - 降低小数据包的延迟
2. **IOCP关联**：
   - 将客户端套接字关联到完成端口
   - 使用套接字自身作为完成键
3. **连接管理**：
   - 添加到客户端映射表
   - 使用互斥锁保证线程安全
4. **超时处理**：
   - 添加120秒超时定时器
   - 超时后自动关闭连接
5. **I/O操作**：
   - 投递异步接收操作
   - 继续接受新连接
   - 清理Accept数据

### 5. 静态文件服务（iocp_server.cpp）

```c++
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
```

1. **请求处理**：
   - 默认请求路径为/index.html
   - 检查路径安全性防止目录遍历
2. **文件操作**：
   - 构建完整文件路径
   - 检查文件是否存在
   - 读取文件内容到内存
3. **MIME类型**：
   - 根据文件扩展名设置Content-Type
   - 支持常见文件类型
4. **响应构建**：
   - 调用BuildHttpResponse构建完整响应
   - 包含状态码、头部和内容
5. **异步发送**：
   - 创建Send操作数据结构
   - 投递异步发送操作
   - 响应完成后自动清理资源