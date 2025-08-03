#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include <string>
#include <unordered_map>

// HTTP方法枚举
enum class HttpMethod { 
    UNKNOWN = 0,
    GET = 1, 
    POST = 2
};

// 解析状态枚举
enum class ParseStatus { 
    SUCCESS,    // 解析成功
    INCOMPLETE, // 数据不完整
    FAILED      // 解析失败
};

// HTTP请求结构体
struct HttpRequest {
    HttpMethod method = HttpMethod::UNKNOWN;  // 请求方法
    std::string uri;                         // 请求URI
    std::string version;                     // HTTP版本
    std::unordered_map<std::string, std::string> headers;  // 请求头
    std::string body;                        // 请求体
};

// HTTP解析器类
class HttpParser {
public:
    HttpParser();
    void reset();  // 重置解析器状态
    ParseStatus parse(const char* data, size_t length);  // 解析HTTP数据
    const HttpRequest& request() const;  // 获取解析后的请求
    
private:
    // 解析状态枚举
    enum class State {
        METHOD,       // 解析方法
        URI,          // 解析URI
        VERSION,      // 解析版本
        HEADER_NAME,  // 解析头名称
        HEADER_VALUE, // 解析头值
        BODY,         // 解析体
        COMPLETE      // 完成解析
    };
    
    State state_;            // 当前解析状态
    HttpRequest request_;    // 解析结果
    std::string current_header_;  // 当前正在解析的头字段
    size_t content_length_;  // 内容长度
    size_t bytes_remaining_; // 剩余待解析字节数
};

#endif