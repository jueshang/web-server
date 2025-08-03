#ifndef COMMON_HPP
#define COMMON_HPP

// 包含必要的Windows网络和系统头文件
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>

// 包含C++标准库头文件
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <ostream>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <filesystem>
#include <functional>

// 针对MinGW和MSVC的不同链接设置
#ifdef __MINGW32__
#define LINK_WINSOCK
#else
#pragma comment(lib, "ws2_32.lib")  // 链接Winsock库
#pragma comment(lib, "mswsock.lib")  // 链接微软扩展库
#endif

// 获取默认线程数(CPU核心数*2，最小为4)
inline int GetDefaultThreadCount() {
    static const int count = std::thread::hardware_concurrency() > 0 ? 
                           std::thread::hardware_concurrency() * 2 : 4;
    return count;
}

// 默认端口号
const int DEFAULT_PORT = 8080;
// 缓冲区大小
const int BUFFER_SIZE = 8192;
// 最大并发连接数
const int MAX_CONCURRENT = 2000;

// I/O操作类型枚举
enum class IoOperation { ACCEPT, RECV, SEND };

// 每个I/O操作的数据结构
struct PerIoData {
    OVERLAPPED overlapped;  // Windows重叠I/O结构
    WSABUF wsaBuf;          // Winsock缓冲区结构
    IoOperation operation;  // 操作类型
    SOCKET socket;          // 关联的套接字
    char buffer[BUFFER_SIZE]; // 数据缓冲区
    
    // 默认构造函数
    PerIoData() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));  // 清空重叠结构
        wsaBuf.len = sizeof(buffer);  // 设置缓冲区长度
        wsaBuf.buf = buffer;          // 设置缓冲区指针
    }
    
    // 带参数的构造函数
    PerIoData(SOCKET s, IoOperation op) : PerIoData() {
        socket = s;      // 设置套接字
        operation = op;  // 设置操作类型
    }
    
    // 带数据初始化的构造函数
    PerIoData(SOCKET s, IoOperation op, const std::string& data) : PerIoData(s, op) {
        size_t copySize = std::min(data.size(), sizeof(buffer));  // 计算可拷贝大小
        memcpy(buffer, data.data(), copySize);  // 拷贝数据
        wsaBuf.len = static_cast<ULONG>(copySize);  // 设置实际数据长度
    }
};

// 打印Windows错误信息
inline void PrintWindowsError(DWORD errorCode) {
    LPSTR messageBuffer = nullptr;
    // 获取系统错误信息
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        nullptr);
    
    if (messageBuffer) {
        std::cerr << messageBuffer;  // 输出错误信息
        LocalFree(messageBuffer);    // 释放缓冲区
    } else {
        std::cerr << "Unknown error";  // 未知错误
    }
    std::cerr << std::endl;
}

#endif