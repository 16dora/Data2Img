#pragma once
#include "image_codec.h"

namespace d2i
{
// 输入：编码配置；输出：结果；作用：流式编码文件并逐帧提交 PNG。
D2iReport encodeFile(const D2iEncodeOptions& options);
// 输入：解码配置；输出：结果；作用：按内嵌序号写入暂存文件并验证整文件。
D2iReport decodeFiles(const D2iDecodeOptions& options);
}
