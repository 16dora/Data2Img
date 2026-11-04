#include "support.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace d2i
{
// 输入：状态、文本；输出：异常；作用：携带跨层错误原因。
Failure::Failure(int code, const std::string& text) : std::runtime_error(text), m_code(code)
{
}
// 输入：无；输出：状态；作用：读取错误分类。
int Failure::getCode() const { return m_code; }

// 输入：无；输出：单例；作用：保持唯一日志入口。
Logger& Logger::getInstance()
{
    static Logger instance;
    return instance;
}
// 输入：详细标志；输出：成功与否；作用：初始化借用的日志句柄。
bool Logger::initialize(bool verbose)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handle = GetStdHandle(STD_ERROR_HANDLE);
    m_isReady = m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    m_isVerbose = verbose;
    return m_isReady;
}
// 输入：无；输出：是否就绪；作用：支持宿主初始化检查。
bool Logger::isReady() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isReady;
}
// 输入：级别、文本；输出：无；作用：统一格式和线程同步。
void Logger::write(const char* level, const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isReady || (!m_isVerbose && std::string(level) == "DEBUG"))
    {
        return;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::ostringstream line;
    line << std::setfill('0') << std::setw(4) << now.wYear << '-' << std::setw(2)
         << now.wMonth << '-' << std::setw(2) << now.wDay << ' ' << std::setw(2)
         << now.wHour << ':' << std::setw(2) << now.wMinute << ':' << std::setw(2)
         << now.wSecond << " [Data2Img] " << level << ' ' << text << '\n';
    const std::string bytes = line.str();
    DWORD written = 0;
    WriteFile(m_handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}
// 输入：错误码；输出：文本；作用：保留系统失败信息。
std::string systemError(DWORD code)
{
    wchar_t buffer[1024]{};
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer, 1024, nullptr);
    return "Win32=" + std::to_string(code) + " " + toUtf8(std::wstring(buffer, size));
}
// 输入：Unicode；输出：UTF-8；作用：路径日志编码转换。
std::string toUtf8(const std::wstring& value)
{
    if (value.empty()) { return {}; }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) { throw Failure(D2I_ARGUMENT, "Invalid UTF-16 string"); }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
// 输入：字节；输出：十六进制；作用：格式化摘要。
std::string toHex(const uint8_t* data, size_t size)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) { out << std::setw(2) << static_cast<unsigned>(data[i]); }
    return out.str();
}
// 输入：字节；输出：CRC；作用：IEEE 多项式的确定性校验。
uint32_t calculateCrc(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xffffffffU;
}
// 输入：路径；输出：有界字节；作用：禁止超大压缩图片文件分配。
std::vector<uint8_t> readImageBytes(const std::filesystem::path& path)
{
    const uint64_t size = std::filesystem::file_size(path);
    if (size < 4 || size > 256ULL * 1024 * 1024)
    {
        throw Failure(D2I_FORMAT, "Image file size outside [4, 268435456]: " + toUtf8(path.wstring()));
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!in || !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        throw Failure(D2I_IO, "Cannot read image: " + toUtf8(path.wstring()));
    }
    return bytes;
}
// 输入：路径、字节；输出：无；作用：写入并检查缓冲区落盘错误。
void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) { throw Failure(D2I_IO, "Cannot write file: " + toUtf8(path.wstring())); }
}
// 输入：无；输出：哈希实例；作用：建立 CNG 资源并正确处理部分初始化。
Hasher::Hasher()
{
    Logger::getInstance().write("DEBUG", "BCryptOpenAlgorithmProvider >> SHA256");
    const NTSTATUS opened = BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    Logger::getInstance().write("DEBUG", "BCryptOpenAlgorithmProvider << status=" + std::to_string(opened));
    if (opened < 0) { throw Failure(D2I_INTERNAL, "Cannot open CNG SHA256: " + std::to_string(opened)); }
    try
    {
        DWORD bytes = 0;
        DWORD actual = 0;
        Logger::getInstance().write("DEBUG", "BCryptGetProperty >> BCRYPT_OBJECT_LENGTH");
        const NTSTATUS queried = BCryptGetProperty(m_alg, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &actual, 0);
        Logger::getInstance().write("DEBUG", "BCryptGetProperty << status=" + std::to_string(queried) + " bytes=" + std::to_string(bytes));
        if (queried < 0) { throw Failure(D2I_INTERNAL, "Cannot query CNG object size"); }
        m_storage.resize(bytes);
        Logger::getInstance().write("DEBUG", "BCryptCreateHash >> bytes=" + std::to_string(bytes));
        const NTSTATUS created = BCryptCreateHash(m_alg, &m_hash, m_storage.data(), bytes, nullptr, 0, 0);
        Logger::getInstance().write("DEBUG", "BCryptCreateHash << status=" + std::to_string(created));
        if (created < 0) { throw Failure(D2I_INTERNAL, "Cannot create CNG hash"); }
    }
    catch (...)
    {
        BCryptCloseAlgorithmProvider(m_alg, 0);
        m_alg = nullptr;
        throw;
    }
}
// 输入：无；输出：无；作用：销毁当前实例持有的句柄。
Hasher::~Hasher()
{
    if (m_hash) { BCryptDestroyHash(m_hash); }
    if (m_alg) { BCryptCloseAlgorithmProvider(m_alg, 0); }
}
// 输入：块；输出：无；作用：按 ULONG 边界追加数据。
void Hasher::append(const uint8_t* data, size_t size)
{
    while (size > 0)
    {
        const ULONG count = static_cast<ULONG>((std::min)(size, static_cast<size_t>(1024 * 1024)));
        Logger::getInstance().write("DEBUG", "BCryptHashData >> bytes=" + std::to_string(count));
        const NTSTATUS status = BCryptHashData(m_hash, const_cast<PUCHAR>(data), count, 0);
        Logger::getInstance().write("DEBUG", "BCryptHashData << status=" + std::to_string(status));
        if (status < 0) { throw Failure(D2I_INTERNAL, "CNG hash update failed"); }
        size -= count;
        data += count;
    }
}
// 输入：无；输出：摘要；作用：完成 SHA256。
std::array<uint8_t, 32> Hasher::finish()
{
    std::array<uint8_t, 32> digest{};
    Logger::getInstance().write("DEBUG", "BCryptFinishHash >>");
    const NTSTATUS status = BCryptFinishHash(m_hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    Logger::getInstance().write("DEBUG", "BCryptFinishHash << status=" + std::to_string(status));
    if (status < 0) { throw Failure(D2I_INTERNAL, "CNG hash finish failed"); }
    return digest;
}
// 输入：路径；输出：摘要；作用：限定内存的文件哈希计算。
std::array<uint8_t, 32> hashFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { throw Failure(D2I_IO, "Cannot open file: " + toUtf8(path.wstring())); }
    Hasher hash;
    std::array<uint8_t, 65536> buffer{};
    while (in)
    {
        in.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        hash.append(buffer.data(), static_cast<size_t>(in.gcount()));
    }
    if (!in.eof()) { throw Failure(D2I_IO, "Read error during SHA256"); }
    return hash.finish();
}
// 输入：目标、覆盖标志；输出：暂存对象；作用：以独占创建避免清理他人文件。
StagedFile::StagedFile(const std::filesystem::path& target, bool overwrite)
    : m_target(std::filesystem::absolute(target)), m_canOverwrite(overwrite)
{
    if (std::filesystem::exists(m_target) && !m_canOverwrite)
    {
        throw Failure(D2I_IO, "Output exists; use --overwrite: " + toUtf8(m_target.wstring()));
    }
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        uint64_t nonce = 0;
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&nonce), sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
            throw Failure(D2I_INTERNAL, "Cannot create temporary filename");
        }
        m_temp = m_target.wstring() + L".partial." + std::to_wstring(nonce);
        const HANDLE file = CreateFileW(m_temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); return; }
        const DWORD code = GetLastError();
        if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
        {
            throw Failure(D2I_IO, "Cannot create temporary output: " + systemError(code));
        }
    }
    throw Failure(D2I_IO, "Cannot reserve a unique temporary output");
}
// 输入：无；输出：无；作用：失败路径自动清理自己拥有的暂存文件。
StagedFile::~StagedFile()
{
    if (!m_isCommitted)
    {
        std::error_code error;
        std::filesystem::remove(m_temp, error);
    }
}
// 输入：无；输出：路径；作用：提供暂存写入位置。
const std::filesystem::path& StagedFile::getPath() const { return m_temp; }
// 输入：无；输出：无；作用：确认落盘并按覆盖策略执行原子替换。
void StagedFile::commit()
{
    const HANDLE file = CreateFileW(m_temp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) { throw Failure(D2I_IO, "Cannot reopen temporary output"); }
    const BOOL flushed = FlushFileBuffers(file);
    const DWORD code = GetLastError();
    CloseHandle(file);
    if (!flushed) { throw Failure(D2I_IO, "Flush failed: " + systemError(code)); }
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (m_canOverwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(m_temp.c_str(), m_target.c_str(), flags))
    {
        throw Failure(D2I_IO, "Cannot commit output: " + systemError(GetLastError()));
    }
    m_isCommitted = true;
}
}
