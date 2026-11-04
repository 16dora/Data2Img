#pragma once
#include "support.h"
extern "C"
{
#include <correct.h>
}
#include <memory>

namespace d2i
{
// 固定外边距，单位物理像素。
static constexpr uint32_t MARGIN_PX = 32;
// RS 每码字的消息字节数。
static constexpr size_t RS_K = 191;
// RS 每码字的总字节数。
static constexpr size_t RS_N = 255;
// 单份头部占用的四色位符号数量。
static constexpr size_t HEADER_SYMBOLS = RS_N * 2;
// 每张图片主网格内的头部副本数。
static constexpr size_t HEADER_COPIES = 3;
// 最大图像像素数，用于限制解码和渲染内存。
static constexpr uint64_t MAX_PIXELS = 64000000;
// 图片单边上限，单位像素。
static constexpr uint32_t MAX_SIDE_PX = 16384;
// 最大正方形色块边长，单位像素。
static constexpr uint32_t MAX_CELL_PX = 32;

// 当前帧的几何布局与派生容量。
struct Layout
{
    uint32_t widthPx = 3840; // 画布宽度。
    uint32_t heightPx = 2060; // 画布高度。
    uint32_t cellPx = 2; // 色块边长。
    uint32_t cols = 0; // 网格列数。
    uint32_t rows = 0; // 网格行数。
    size_t symbols = 0; // 网格符号总数。
    uint32_t capacity = 0; // 最大有效字节数。
};

// 逻辑图片头，显式序列化而不使用结构体内存布局。
struct Header
{
    Layout layout; // 本文件统一布局。
    uint32_t frameIndex = 0; // 从零开始的帧序号。
    uint32_t frameCount = 0; // 总帧数。
    uint64_t fileSize = 0; // 完整文件大小。
    uint64_t chunkOffset = 0; // 本帧起始偏移。
    uint32_t chunkSize = 0; // 本帧有效数据长度。
    uint32_t chunkCrc = 0; // 有效数据 CRC32。
    std::array<uint8_t, 32> sha{}; // 完整文件 SHA-256。
    uint32_t blocks = 0; // 有效数据的 RS 码字数。
};

// 输入：画布和色块尺寸；输出：布局；作用：校验参数并计算精确容量。
Layout makeLayout(uint32_t widthPx, uint32_t heightPx, uint32_t cellPx);
// 输入：符号索引、布局；输出：是否为主网格头部；作用：统一渲染和采样占位。
bool isHeaderSymbol(size_t index, const Layout& layout);
// 输入：布局；输出：三份头部起点；作用：确定分散头部的位置。
std::array<size_t, 3> headerStarts(const Layout& layout);
// 输入：逻辑头；输出：191 字节；作用：按小端序生成带 CRC 的版本 2 头。
std::array<uint8_t, RS_K> packHeader(const Header& header);
// 输入：191 字节；输出：头；作用：验证所有边界和自描述参数。
Header unpackHeader(const std::array<uint8_t, RS_K>& bytes);
// 输入：两帧头；输出：是否同一文件与格式；作用：阻止混帧。
bool isSameFile(const Header& left, const Header& right);

// 独占一个 libcorrect 实例，不跨线程共享。
class ReedSolomon
{
public:
    // 输入：无；输出：实例；作用：固定 RS(255,191) 参数。
    ReedSolomon();
    // 输入：无；输出：无；作用：释放纠错实例。
    ~ReedSolomon();
    ReedSolomon(const ReedSolomon&) = delete;
    ReedSolomon& operator=(const ReedSolomon&) = delete;
    // 输入：191 字节；输出：255 字节；作用：生成独立纠错码字。
    std::array<uint8_t, RS_N> encode(const std::array<uint8_t, RS_K>& data);
    // 输入：255 字节；输出：成功时写入消息；作用：修正单码字错误。
    bool decode(const std::array<uint8_t, RS_N>& data, std::array<uint8_t, RS_K>& out);
private:
    std::unique_ptr<correct_reed_solomon, decltype(&correct_reed_solomon_destroy)> m_rs;
};
// 输入：负载、RS 实例；输出：按字节交织流；作用：分散局部损坏。
std::vector<uint8_t> encodePayload(const std::vector<uint8_t>& chunk, ReedSolomon& rs);
// 输入：交织流、头、RS 实例；输出：有效负载；作用：反交织、纠错并验证 CRC。
std::vector<uint8_t> decodePayload(const std::vector<uint8_t>& stream,
    const Header& header, ReedSolomon& rs);
}
