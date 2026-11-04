#include "file_codec.h"
#include <algorithm>
#include <cstring>

namespace d2i
{
// 输入：错误缓冲区、大小、文本；输出：无；作用：绝不向调用方缓冲区外写入。
static void copyError(char* error, size_t errorBytes, const char* text)
{
    if (!error || errorBytes == 0) { return; }
    const size_t count = (std::min)(errorBytes - 1, std::strlen(text));
    std::memcpy(error, text, count);
    error[count] = '\0';
}
// 输入：两类配置之一、输出；输出：状态；作用：统一异常边界和关键流程退出日志。
static int runApi(const D2iEncodeOptions* encode, const D2iDecodeOptions* decode,
    D2iReport* report, char* error, size_t errorBytes)
{
    copyError(error, errorBytes, "");
    const char* operation = encode ? "d2i_encode()" : "d2i_decode()";
    try
    {
        Logger::getInstance().write("INFO", std::string(operation) + " >>");
        if (!Logger::getInstance().isReady()) { throw Failure(D2I_ARGUMENT, "Call d2i_init_logging before encoding/decoding"); }
        if (!report || report->structBytes != sizeof(D2iReport)) { throw Failure(D2I_ARGUMENT, "Invalid report structBytes"); }
        if (encode)
        {
            if (encode->structBytes != sizeof(D2iEncodeOptions) || !encode->inputPath || !encode->outputDir ||
                !encode->inputPath[0] || !encode->outputDir[0] || encode->overwrite > 1)
            {
                throw Failure(D2I_ARGUMENT, "Invalid encode options");
            }
            *report = encodeFile(*encode);
        }
        else if (decode)
        {
            if (decode->structBytes != sizeof(D2iDecodeOptions) || !decode->inputs || decode->inputCount == 0 ||
                !decode->outputPath || !decode->outputPath[0] || decode->overwrite > 1 || decode->recursive > 1)
            {
                throw Failure(D2I_ARGUMENT, "Invalid decode options");
            }
            *report = decodeFiles(*decode);
        }
        else { throw Failure(D2I_ARGUMENT, "Null options"); }
        Logger::getInstance().write("INFO", std::string(operation) + " << result=0");
        return D2I_OK;
    }
    catch (const Failure& failure)
    {
        copyError(error, errorBytes, failure.what());
        Logger::getInstance().write("ERROR", std::string(operation) + " << result=" + std::to_string(failure.getCode()) + " " + failure.what());
        return failure.getCode();
    }
    catch (const std::filesystem::filesystem_error& failure)
    {
        copyError(error, errorBytes, failure.what());
        Logger::getInstance().write("ERROR", std::string(operation) + " << result=2 " + failure.what());
        return D2I_IO;
    }
    catch (const cv::Exception& failure)
    {
        copyError(error, errorBytes, failure.what());
        Logger::getInstance().write("ERROR", std::string(operation) + " << result=3 " + failure.what());
        return D2I_FORMAT;
    }
    catch (const std::exception& failure)
    {
        copyError(error, errorBytes, failure.what());
        Logger::getInstance().write("ERROR", std::string(operation) + " << result=8 " + failure.what());
        return D2I_INTERNAL;
    }
    catch (...)
    {
        copyError(error, errorBytes, "Unknown core exception");
        Logger::getInstance().write("ERROR", std::string(operation) + " << result=8 unknown exception");
        return D2I_INTERNAL;
    }
}
}
// 输入：详细标志；输出：状态；作用：初始化公共日志，失败路径不依赖日志。
int d2i_init_logging(uint32_t verbose)
{
    try { return verbose <= 1 && d2i::Logger::getInstance().initialize(verbose != 0) ? D2I_OK : D2I_INTERNAL; }
    catch (...) { return D2I_INTERNAL; }
}
// 输入：编码配置及调用者缓冲区；输出：状态；作用：提供稳定 C 导出入口。
int d2i_encode(const D2iEncodeOptions* options, D2iReport* report, char* error, size_t errorBytes)
{
    try { return d2i::runApi(options, nullptr, report, error, errorBytes); }
    catch (...)
    {
        // 连异常处理过程本身也可能内存不足；最外层使用固定文本保证 ABI 不抛异常。
        d2i::copyError(error, errorBytes, "Core could not allocate error diagnostics");
        return D2I_INTERNAL;
    }
}
// 输入：解码配置及调用者缓冲区；输出：状态；作用：提供稳定 C 导出入口。
int d2i_decode(const D2iDecodeOptions* options, D2iReport* report, char* error, size_t errorBytes)
{
    try { return d2i::runApi(nullptr, options, report, error, errorBytes); }
    catch (...)
    {
        // 此边界不再构造动态字符串，避免异常越过 C 接口。
        d2i::copyError(error, errorBytes, "Core could not allocate error diagnostics");
        return D2I_INTERNAL;
    }
}
// 输入：级别、文本；输出：无；作用：保持日志入口无异常越过 DLL。
void d2i_log_message(const char* level, const char* text)
{
    try
    {
        if (level && text) { d2i::Logger::getInstance().write(level, text); }
    }
    catch (...) { }
}
