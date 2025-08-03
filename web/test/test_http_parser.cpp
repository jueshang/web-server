#include "http_parser.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
#include <string>  // 添加string头文件

const char* method_to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        default: return "UNKNOWN";
    }
}

void run_test(const std::string& name, const std::string& request, bool should_pass) {
    HttpParser parser;
    ParseStatus result = parser.parse(request.c_str(), request.length());
    
    std::cout << "\nTest: " << name << "\n";
    std::cout << "Request (" << request.length() << " bytes):\n" 
              << "----------------------------------------\n"
              << request
              << "\n----------------------------------------\n";
    
    bool success = (result == ParseStatus::SUCCESS);
    if (success == should_pass) {
        std::cout << "[PASS] " << name << "\n";
        if (success) {
            const auto& req = parser.request();
            std::cout << "Parsed request:\n";
            std::cout << "Method: " << method_to_string(req.method) << "\n";
            std::cout << "URI: " << req.uri << "\n";
            std::cout << "Version: " << req.version << "\n";
            if (!req.body.empty()) {
                std::cout << "Body length: " << req.body.size() << "\n";
            }
        }
    } else {
        std::cout << "[FAIL] " << name << "\n";
        std::cout << "Expected: " << (should_pass ? "SUCCESS" : "FAILURE") 
                  << ", Got: " << static_cast<int>(result) << "\n";
        assert(false);
    }
}

int main() {
    std::cout << "Starting HTTP Parser Tests...\n";
    
    // 基本测试
    run_test("1. Minimal GET", 
        "GET / HTTP/1.1\r\n"
        "\r\n", 
        true);
    
    run_test("2. GET with headers", 
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: keep-alive\r\n"
        "\r\n", 
        true);
    
    run_test("3. POST with body", 
        "POST /submit HTTP/1.1\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world", 
        true);
    
    // 构建长头部测试用例
    std::string long_header_test = 
        "GET / HTTP/1.1\r\n"
        "Very-Long-Header: " + std::string(1000, 'a') + "\r\n"
        "\r\n";
    
    run_test("4. Long header", long_header_test, true);
    
    run_test("5. Invalid request", 
        "INVALID REQUEST\r\n", 
        false);
    
    std::cout << "\nAll tests completed!\n";
    return 0;
}