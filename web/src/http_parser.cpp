#include "http_parser.hpp"
#include <algorithm>
#include <cctype>

// 构造函数，初始化状态
HttpParser::HttpParser() : 
    state_(State::METHOD), 
    content_length_(0),
    bytes_remaining_(0) {}

// 重置解析器状态
void HttpParser::reset() {
    state_ = State::METHOD;
    request_ = HttpRequest{};
    current_header_.clear();
    content_length_ = 0;
    bytes_remaining_ = 0;
}

// 解析HTTP数据
ParseStatus HttpParser::parse(const char* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        char c = data[i];
        
        switch (state_) {
            case State::METHOD:
                if (c == ' ') {
                    // 确定HTTP方法
                    if (current_header_ == "GET") {
                        request_.method = HttpMethod::GET;
                    } else if (current_header_ == "POST") {
                        request_.method = HttpMethod::POST;
                    } else {
                        return ParseStatus::FAILED;  // 未知方法
                    }
                    current_header_.clear();
                    state_ = State::URI;
                } else {
                    current_header_ += c;  // 累积方法字符
                }
                break;
                
            case State::URI:
                if (c == ' ') {
                    state_ = State::VERSION;  // 转到版本解析
                } else {
                    request_.uri += c;  // 累积URI字符
                }
                break;
                
            case State::VERSION:
                if (c == '\r') {
                    state_ = State::HEADER_NAME;  // 转到头解析
                } else if (c != '\n') {
                    request_.version += c;  // 累积版本字符
                }
                break;
                
            case State::HEADER_NAME:
                if (c == ':') {
                    state_ = State::HEADER_VALUE;  // 转到头值解析
                } else if (c == '\r') {
                    // 检查是否需要解析体
                    if (request_.method == HttpMethod::POST && content_length_ > 0) {
                        bytes_remaining_ = content_length_;
                        state_ = State::BODY;
                    } else {
                        state_ = State::COMPLETE;  // 完成解析
                    }
                } else {
                    current_header_ += c;  // 累积头名字符
                }
                break;
                
            case State::HEADER_VALUE:
                if (c == '\r') {
                    // 处理头值
                    std::string value = current_header_;
                    value.erase(0, value.find_first_not_of(" \t"));  // 去除前导空白
                    value.erase(value.find_last_not_of(" \t") + 1);   // 去除尾部空白
                    
                    // 特殊处理Content-Length头
                    if (current_header_ == "Content-Length") {
                        try {
                            content_length_ = std::stoul(value);  // 转换内容长度
                        } catch (...) {
                            return ParseStatus::FAILED;  // 转换失败
                        }
                    }
                    
                    // 存储头字段
                    request_.headers[current_header_] = value;
                    current_header_.clear();
                    state_ = State::HEADER_NAME;
                } else if (c != '\n') {
                    current_header_ += c;  // 累积头值字符
                }
                break;
                
            case State::BODY:
                request_.body += c;  // 累积体数据
                if (--bytes_remaining_ == 0) {
                    state_ = State::COMPLETE;  // 完成体解析
                }
                break;
                
            case State::COMPLETE:
                return ParseStatus::SUCCESS;  // 返回成功
        }
    }
    
    // 返回当前状态
    return (state_ == State::COMPLETE) ? ParseStatus::SUCCESS : ParseStatus::INCOMPLETE;
}

// 获取解析后的请求
const HttpRequest& HttpParser::request() const {
    return request_;
}