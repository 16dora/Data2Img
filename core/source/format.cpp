#include "format.h"
#include <algorithm>
#include <cstring>

namespace d2i
{
// 输入：画布、色块；输出：布局；作用：按向下取整的网格计算容量。
Layout makeLayout(uint32_t widthPx, uint32_t heightPx, uint32_t cellPx)
{
    if (widthPx < 384 || heightPx < 128 || widthPx > MAX_SIDE_PX || heightPx > MAX_SIDE_PX ||
        static_cast<uint64_t>(widthPx) * heightPx > MAX_PIXELS || cellPx == 0 || cellPx > MAX_CELL_PX)
    {
        throw Failure(D2I_ARGUMENT, "Require width 384..16384, height 128..16384, <=64M pixels, cell 1..32");
    }
    Layout layout;
    layout.widthPx = widthPx;
    layout.heightPx = heightPx;
    layout.cellPx = cellPx;
    layout.cols = (widthPx - 2 * MARGIN_PX) / cellPx;
    layout.rows = (heightPx - 2 * MARGIN_PX) / cellPx;
    layout.symbols = static_cast<size_t>(layout.cols) * layout.rows;
    if (layout.symbols < HEADER_SYMBOLS * (HEADER_COPIES + 1))
    {
        throw Failure(D2I_ARGUMENT, "Grid too small for three headers and one payload RS block");
    }
    const size_t blocks = (layout.symbols - HEADER_SYMBOLS * HEADER_COPIES) / (RS_N * 2);
    layout.capacity = static_cast<uint32_t>(blocks * RS_K);
    return layout;
}
// 输入：布局；输出：起点；作用：保持三份头部不重叠。
std::array<size_t, 3> headerStarts(const Layout& layout)
{
    return {0, (layout.symbols - HEADER_SYMBOLS) / 2, layout.symbols - HEADER_SYMBOLS};
}
// 输入：索引、布局；输出：头部标志；作用：跳过所有保留符号。
bool isHeaderSymbol(size_t index, const Layout& layout)
{
    for (const size_t start : headerStarts(layout))
    {
        if (index >= start && index < start + HEADER_SYMBOLS) { return true; }
    }
    return false;
}
// 输入：数组、偏移、整数、字节数；输出：无；作用：明确小端写入。
static void writeLe(std::array<uint8_t, RS_K>& bytes, size_t offset, uint64_t value, size_t count)
{
    for (size_t i = 0; i < count; ++i) { bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i)); }
}
// 输入：数组、偏移、字节数；输出：整数；作用：明确小端读取。
static uint64_t readLe(const std::array<uint8_t, RS_K>& bytes, size_t offset, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) { value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i); }
    return value;
}
// 输入：头；输出：字节；作用：生成版本 2 自描述头及 CRC。
std::array<uint8_t, RS_K> packHeader(const Header& header)
{
    std::array<uint8_t, RS_K> bytes{};
    std::memcpy(bytes.data(), "SCIMG002", 8);
    writeLe(bytes, 8, 2, 2);
    writeLe(bytes, 10, RS_K, 2);
    writeLe(bytes, 12, header.layout.widthPx, 2);
    writeLe(bytes, 14, header.layout.heightPx, 2);
    bytes[16] = static_cast<uint8_t>(header.layout.cellPx);
    bytes[17] = 1;
    bytes[18] = static_cast<uint8_t>(RS_K);
    bytes[19] = static_cast<uint8_t>(RS_N);
    writeLe(bytes, 24, header.frameIndex, 4);
    writeLe(bytes, 28, header.frameCount, 4);
    writeLe(bytes, 32, header.fileSize, 8);
    writeLe(bytes, 40, header.chunkOffset, 8);
    writeLe(bytes, 48, header.chunkSize, 4);
    writeLe(bytes, 52, header.chunkCrc, 4);
    std::copy(header.sha.begin(), header.sha.end(), bytes.begin() + 56);
    std::copy_n(header.sha.begin(), 16, bytes.begin() + 88);
    writeLe(bytes, 104, header.blocks, 4);
    writeLe(bytes, 108, header.layout.capacity, 4);
    writeLe(bytes, 187, calculateCrc(bytes.data(), 187), 4);
    return bytes;
}
// 输入：字节；输出：有效头；作用：分配负载前完整验证所有字段。
Header unpackHeader(const std::array<uint8_t, RS_K>& bytes)
{
    if (std::memcmp(bytes.data(), "SCIMG002", 8) != 0 || readLe(bytes, 8, 2) != 2 ||
        readLe(bytes, 10, 2) != RS_K || bytes[17] != 1 || bytes[18] != RS_K || bytes[19] != RS_N ||
        readLe(bytes, 20, 4) != 0 || readLe(bytes, 187, 4) != calculateCrc(bytes.data(), 187))
    {
        throw Failure(D2I_FORMAT, "Invalid version-2 header, flags, RS parameters or CRC");
    }
    for (size_t i = 112; i < 187; ++i)
    {
        if (bytes[i] != 0) { throw Failure(D2I_FORMAT, "Nonzero reserved header byte"); }
    }
    Header header;
    try
    {
        header.layout = makeLayout(static_cast<uint32_t>(readLe(bytes, 12, 2)),
            static_cast<uint32_t>(readLe(bytes, 14, 2)), bytes[16]);
    }
    catch (const Failure& error) { throw Failure(D2I_FORMAT, error.what()); }
    header.frameIndex = static_cast<uint32_t>(readLe(bytes, 24, 4));
    header.frameCount = static_cast<uint32_t>(readLe(bytes, 28, 4));
    header.fileSize = readLe(bytes, 32, 8);
    header.chunkOffset = readLe(bytes, 40, 8);
    header.chunkSize = static_cast<uint32_t>(readLe(bytes, 48, 4));
    header.chunkCrc = static_cast<uint32_t>(readLe(bytes, 52, 4));
    std::copy_n(bytes.begin() + 56, 32, header.sha.begin());
    header.blocks = static_cast<uint32_t>(readLe(bytes, 104, 4));
    const uint64_t capacity = header.layout.capacity;
    const uint64_t count = header.fileSize == 0 ? 1 : 1 + (header.fileSize - 1) / capacity;
    if (count > UINT32_MAX || header.fileSize > static_cast<uint64_t>(INT64_MAX) ||
        header.frameCount != count || header.frameIndex >= header.frameCount ||
        header.chunkOffset != static_cast<uint64_t>(header.frameIndex) * capacity ||
        header.chunkSize != (std::min)(capacity, header.fileSize - header.chunkOffset) ||
        header.blocks != (header.chunkSize + RS_K - 1) / RS_K ||
        readLe(bytes, 108, 4) != capacity || !std::equal(header.sha.begin(), header.sha.begin() + 16, bytes.begin() + 88))
    {
        throw Failure(D2I_FORMAT, "Inconsistent frame bounds, size, file ID or capacity");
    }
    return header;
}
// 输入：两头；输出：匹配与否；作用：完整哈希及格式联合分组。
bool isSameFile(const Header& left, const Header& right)
{
    return left.sha == right.sha && left.fileSize == right.fileSize && left.frameCount == right.frameCount &&
        left.layout.widthPx == right.layout.widthPx && left.layout.heightPx == right.layout.heightPx &&
        left.layout.cellPx == right.layout.cellPx;
}
// 输入：无；输出：对象；作用：固定 GF 参数与 64 个校验根。
ReedSolomon::ReedSolomon() : m_rs(nullptr, &correct_reed_solomon_destroy)
{
    Logger::getInstance().write("DEBUG", "correct_reed_solomon_create >> polynomial=285 roots=64");
    m_rs.reset(correct_reed_solomon_create(0x11d, 1, 1, 64));
    Logger::getInstance().write("DEBUG", std::string("correct_reed_solomon_create << ") + (m_rs ? "OK" : "FAIL"));
    if (!m_rs) { throw Failure(D2I_INTERNAL, "Cannot create RS(255,191) codec"); }
}
// 输入：无；输出：无；作用：RAII 释放编码器。
ReedSolomon::~ReedSolomon() = default;
// 输入：消息；输出：码字；作用：检测上游返回长度。
std::array<uint8_t, RS_N> ReedSolomon::encode(const std::array<uint8_t, RS_K>& data)
{
    std::array<uint8_t, RS_N> output{};
    Logger::getInstance().write("DEBUG", "correct_reed_solomon_encode >> bytes=191");
    const ssize_t count = correct_reed_solomon_encode(m_rs.get(), data.data(), data.size(), output.data());
    Logger::getInstance().write("DEBUG", "correct_reed_solomon_encode << bytes=" + std::to_string(count));
    if (count != RS_N) { throw Failure(D2I_INTERNAL, "RS encoder returned an unexpected length"); }
    return output;
}
// 输入：码字；输出：消息与是否成功；作用：由外层 CRC 检查误纠正。
bool ReedSolomon::decode(const std::array<uint8_t, RS_N>& data, std::array<uint8_t, RS_K>& out)
{
    Logger::getInstance().write("DEBUG", "correct_reed_solomon_decode >> bytes=255");
    const ssize_t count = correct_reed_solomon_decode(m_rs.get(), data.data(), data.size(), out.data());
    Logger::getInstance().write("DEBUG", "correct_reed_solomon_decode << bytes=" + std::to_string(count));
    return count == RS_K;
}
// 输入：负载；输出：交织码流；作用：尾块补零后按码字字节交织。
std::vector<uint8_t> encodePayload(const std::vector<uint8_t>& chunk, ReedSolomon& rs)
{
    const size_t blocks = (chunk.size() + RS_K - 1) / RS_K;
    std::vector<uint8_t> stream(blocks * RS_N);
    for (size_t block = 0; block < blocks; ++block)
    {
        std::array<uint8_t, RS_K> data{};
        const size_t count = (std::min)(RS_K, chunk.size() - block * RS_K);
        std::copy_n(chunk.data() + block * RS_K, count, data.data());
        const auto code = rs.encode(data);
        for (size_t byte = 0; byte < RS_N; ++byte) { stream[byte * blocks + block] = code[byte]; }
    }
    return stream;
}
// 输入：码流、头；输出：负载；作用：反交织并逐块纠错，最终验证 CRC。
std::vector<uint8_t> decodePayload(const std::vector<uint8_t>& stream, const Header& header, ReedSolomon& rs)
{
    if (stream.size() != static_cast<size_t>(header.blocks) * RS_N)
    {
        throw Failure(D2I_FORMAT, "Payload stream length mismatch");
    }
    std::vector<uint8_t> chunk;
    chunk.reserve(static_cast<size_t>(header.blocks) * RS_K);
    size_t failed = 0;
    for (size_t block = 0; block < header.blocks; ++block)
    {
        std::array<uint8_t, RS_N> code{};
        std::array<uint8_t, RS_K> data{};
        for (size_t byte = 0; byte < RS_N; ++byte) { code[byte] = stream[byte * header.blocks + block]; }
        if (!rs.decode(code, data)) { ++failed; }
        chunk.insert(chunk.end(), data.begin(), data.end());
    }
    if (failed != 0) { throw Failure(D2I_CORRECTION, "rs_failed_blocks=" + std::to_string(failed)); }
    chunk.resize(header.chunkSize);
    if (calculateCrc(chunk.data(), chunk.size()) != header.chunkCrc)
    {
        throw Failure(D2I_CORRECTION, "rs_failed_blocks=0 crc_pass=false (uncorrectable or miscorrected payload)");
    }
    return chunk;
}
}
