#pragma once
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

// 此宏只控制 DLL 导入导出，不定义业务常量。
#if defined(D2I_BUILD_DLL)
#define D2I_API __declspec(dllexport)
#else
#define D2I_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// API 返回状态，同时对应 CLI 退出码。
typedef enum D2iStatus
{
    // 全部校验通过。
    D2I_OK = 0,
    // 参数或 API 结构体版本错误。
    D2I_ARGUMENT = 1,
    // 文件访问、覆盖或写入失败。
    D2I_IO = 2,
    // PNG/JPEG、定位或协议头无效。
    D2I_FORMAT = 3,
    // RS 或负载 CRC 校验失败。
    D2I_CORRECTION = 4,
    // 缺帧、混入其他文件或重复帧内容冲突。
    D2I_FRAME_SET = 5,
    // 完整文件 SHA-256 不一致。
    D2I_HASH = 6,
    // 内部异常或资源不足。
    D2I_INTERNAL = 8
} D2iStatus;

// 编码参数；路径使用 Windows UTF-16，调用期间由调用者持有。
typedef struct D2iEncodeOptions
{
    uint32_t structBytes; // sizeof(D2iEncodeOptions)，用于检测接口版本。
    const wchar_t* inputPath; // 输入文件路径。
    const wchar_t* outputDir; // PNG 输出目录。
    uint32_t widthPx; // 画布宽度，单位像素。
    uint32_t heightPx; // 画布高度，单位像素。
    uint32_t cellPx; // 正方形色块边长，单位像素。
    uint32_t overwrite; // 0 禁止覆盖，1 允许替换既有输出。
} D2iEncodeOptions;

// 解码参数；PNG/JPG/JPEG 文件和目录可混合传入，所有指针只在调用期间借用。
typedef struct D2iDecodeOptions
{
    uint32_t structBytes; // sizeof(D2iDecodeOptions)。
    const wchar_t* const* inputs; // 输入文件或目录数组。
    uint32_t inputCount; // 输入路径数量。
    const wchar_t* outputPath; // 恢复文件完整路径。
    uint32_t recursive; // 目录是否递归遍历。
    uint32_t overwrite; // 是否允许替换既有目标。
    const char* fileId; // 可选的 32 位十六进制文件 ID；空指针表示不筛选。
} D2iDecodeOptions;

// 成功时填充的固定大小结果，不跨 DLL 分配或释放堆内存。
typedef struct D2iReport
{
    uint32_t structBytes; // 调用前设置 sizeof(D2iReport)。
    uint64_t fileBytes; // 原始文件字节数。
    uint32_t frameCount; // 文件所需帧数。
    uint32_t capacityBytes; // 单帧有效负载上限。
    uint8_t sha256[32]; // 完整文件 SHA-256。
    uint64_t elapsedMs; // 本次操作耗时，单位毫秒。
} D2iReport;

// 初始化统一日志。
// 输入：verbose 为 0/1。
// 输出：状态码。
// 作用：CLI 及 DLL 宿主在业务调用前初始化 stderr 日志入口。
D2I_API int d2i_init_logging(uint32_t verbose);

// 编码文件。
// 输入：配置、结果缓冲区、UTF-8 错误缓冲区及其字节数。
// 输出：状态码；成功填充 report，失败填充以零结尾的 error。
// 作用：按帧流式生成 PNG；任何异常均在 DLL 内转换为状态码。
D2I_API int d2i_encode(const D2iEncodeOptions* options, D2iReport* report,
    char* error, size_t errorBytes);

// 恢复文件。
// 输入：配置、结果缓冲区、UTF-8 错误缓冲区及其字节数。
// 输出：状态码及结果或错误文本。
// 作用：只在所有帧 CRC 和完整 SHA-256 通过后原子提交目标文件。
D2I_API int d2i_decode(const D2iDecodeOptions* options, D2iReport* report,
    char* error, size_t errorBytes);

// 写入统一日志。
// 输入：INFO/WARN/ERROR/DEBUG 级别、UTF-8 消息。
// 输出：无。
// 作用：让 CLI 和未来 GUI 复用 core 的日志入口。
D2I_API void d2i_log_message(const char* level, const char* text);

#ifdef __cplusplus
}
#endif
