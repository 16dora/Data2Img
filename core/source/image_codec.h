#pragma once
#include "format.h"
#include <opencv2/core.hpp>

namespace d2i
{
// 图片解析结果，不在文件集合内缓存全部帧负载。
struct ScannedFrame
{
    Header header; // 经 RS、CRC 和字段校验的头。
    std::vector<uint8_t> chunk; // 经 CRC 校验的单帧数据。
    uint32_t validHeaders = 0; // 通过验证的主网格头部副本数。
};
// 输入：头、负载；输出：BGR 图片；作用：渲染定位区、引导头和数据网格。
cv::Mat renderFrame(const Header& header, const std::vector<uint8_t>& chunk);
// 输入：PNG/JPEG 路径；输出：帧；作用：自动读取布局、定位、取色、纠错和验证。
ScannedFrame scanFrame(const std::filesystem::path& path);
// 输入：PNG/JPEG 路径；输出：有界 BGR 图片；作用：先检查 IHDR/SOF 尺寸再解码。
cv::Mat loadImage(const std::filesystem::path& path);
// 输入：路径、图片；输出：无；作用：生成 PNG 字节后安全写入。
void saveImage(const std::filesystem::path& path, const cv::Mat& image);
}
