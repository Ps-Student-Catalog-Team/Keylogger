#ifndef LOG_UPLOADER_H
#define LOG_UPLOADER_H

#include <string>
#include <vector>

// 日志上传器类
class LogUploader {
private:
    std::string token_;  // 存储认证令牌
    std::string api_base_url_;  // API基础地址

public:
    // 构造函数，可指定API基础地址（默认使用原地址）
    LogUploader(const std::string& api_base = "http://10.88.202.73:5244");

    // 析构函数
    ~LogUploader();

    // 登录并获取token
    bool login(const std::string& username, const std::string& password);

    // 上传文件到服务器
    bool uploadFile(const std::string& localFilePath, const std::string& serverFilePath);

    // 获取程序当前执行目录
    static std::string getExecutableDir();

    // 筛选符合格式的日志文件并返回最新的一个
    static std::string findLatestLogFile(const std::string& dir);

    // 获取当前token（用于调试）
    std::string getToken() const { return token_; }
};

#endif // LOG_UPLOADER_H