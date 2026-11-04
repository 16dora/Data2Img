#pragma once
#include "data2img.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2i
{
// 一次业务失败，携带可直接返回给调用方的状态。
class Failure : public std::runtime_error
{
public:
    // 输入：状态、消息；输出：异常对象；作用：保留原始错误语义。
    Failure(int code, const std::string& text);
    // 输入：无；输出：状态码；作用：读取业务失败类别。
    int getCode() const;
private:
    int m_code; // 业务状态码。
};

// 进程内统一日志入口，不由各模块重复创建日志器。
class Logger
{
public:
    // 输入：无；输出：统一入口；作用：共享日志配置。
    static Logger& getInstance();
    // 输入：详细标志；输出：成功与否；作用：初始化终端日志句柄。
    bool initialize(bool verbose);
    // 输入：无；输出：是否就绪；作用：阻止未初始化的业务调用。
    bool isReady() const;
    // 输入：级别、消息；输出：无；作用：序列化输出带时间戳的日志。
    void write(const char* level, const std::string& text);
private:
    mutable std::mutex m_mutex; // 串行化宿主线程的日志访问。
    HANDLE m_handle = INVALID_HANDLE_VALUE; // 借用 stderr，不负责关闭。
    bool m_isReady = false; // 初始化状态。
    bool m_isVerbose = false; // 是否输出 DEBUG。
};

// 输入：系统错误码；输出：可读字符串；作用：保留 Win32 失败详情。
std::string systemError(DWORD code);
// 输入：Unicode 字符串；输出：UTF-8；作用：日志中保留中文路径。
std::string toUtf8(const std::wstring& value);
// 输入：字节数组；输出：小写十六进制；作用：展示哈希与文件 ID。
std::string toHex(const uint8_t* data, size_t size);
// 输入：字节数组；输出：IEEE CRC32；作用：验证头部和负载。
uint32_t calculateCrc(const uint8_t* data, size_t size);
// 输入：文件路径；输出：文件字节；作用：有限大小读取 PNG 或 JPEG 压缩数据。
std::vector<uint8_t> readImageBytes(const std::filesystem::path& path);
// 输入：路径、字节；输出：无；作用：完整写入并检测关闭失败。
void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data);

// 分块 SHA-256 的独占 CNG 句柄。
class Hasher
{
public:
    // 输入：无；输出：对象；作用：建立 SHA-256 状态。
    Hasher();
    // 输入：无；输出：无；作用：释放 CNG 资源。
    ~Hasher();
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    // 输入：数据及长度；输出：无；作用：追加哈希数据。
    void append(const uint8_t* data, size_t size);
    // 输入：无；输出：摘要；作用：结束当前哈希。
    std::array<uint8_t, 32> finish();
private:
    BCRYPT_ALG_HANDLE m_alg = nullptr; // 算法句柄，当前实例独占。
    BCRYPT_HASH_HANDLE m_hash = nullptr; // 哈希句柄，当前实例独占。
    std::vector<uint8_t> m_storage; // CNG 对象存储。
};

// 输入：路径；输出：摘要；作用：用固定缓冲区计算完整文件哈希。
std::array<uint8_t, 32> hashFile(const std::filesystem::path& path);

// 同目录临时文件，失败时自动清理且不触及原有目标。
class StagedFile
{
public:
    // 输入：目标、覆盖标志；输出：对象；作用：独占创建临时文件。
    StagedFile(const std::filesystem::path& target, bool overwrite);
    // 输入：无；输出：无；作用：清理未提交文件。
    ~StagedFile();
    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;
    // 输入：无；输出：临时路径；作用：让编解码器写入暂存文件。
    const std::filesystem::path& getPath() const;
    // 输入：无；输出：无；作用：原子重命名，最后一刻再次执行覆盖策略。
    void commit();
private:
    std::filesystem::path m_target; // 最终路径。
    std::filesystem::path m_temp; // 独占的暂存路径。
    bool m_canOverwrite; // 是否允许原子替换目标。
    bool m_isCommitted = false; // 提交成功后禁止析构删除。
};
}
