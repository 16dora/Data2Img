#include "image_codec.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>

namespace d2i
{
// 固定引导头的列数，与可配置的主网格无关。
static constexpr int BOOT_COLS = 128;
// 引导头色块边长，单位物理像素。
static constexpr int BOOT_CELL_PX = 2;
// 引导头左上角横坐标，单位物理像素。
static constexpr int BOOT_X_PX = 48;
// 引导头左上角纵坐标，单位物理像素。
static constexpr int BOOT_Y_PX = 4;

// 输入：无；输出：BGR 色表；作用：保持 nibble 与原始 16 色一一对应。
static std::array<cv::Vec3b, 16> makePalette()
{
    return {{
        {0, 0, 0},       // 0 黑色。
        {255, 255, 255}, // 1 白色。
        {0, 0, 255},     // 2 红色。
        {0, 255, 0},     // 3 绿色。
        {255, 0, 0},     // 4 蓝色。
        {0, 255, 255},   // 5 黄色。
        {255, 0, 255},   // 6 品红。
        {255, 255, 0},   // 7 青色。
        {128, 128, 128}, // 8 灰色。
        {0, 0, 128},     // 9 暗红。
        {0, 128, 0},     // 10 暗绿。
        {128, 0, 0},     // 11 暗蓝。
        {0, 128, 128},   // 12 橄榄色。
        {128, 0, 128},   // 13 紫色。
        {128, 128, 0},   // 14 暗青。
        {0, 128, 255}    // 15 橙色。
    }};
}
// 输入：图片、区域、颜色；输出：无；作用：按行填充避免逐符号绘图调用。
static void paintCell(cv::Mat& image, int xPx, int yPx, int sidePx, const cv::Vec3b& color)
{
    for (int y = yPx; y < yPx + sidePx; ++y)
    {
        cv::Vec3b* row = image.ptr<cv::Vec3b>(y);
        std::fill(row + xPx, row + xPx + sidePx, color);
    }
}
// 输入：图片；输出：无；作用：绘制四角同心方块及右上方向标记。
static void paintMarkers(cv::Mat& image)
{
    for (int corner = 0; corner < 4; ++corner)
    {
        const int xPx = (corner == 1 || corner == 2) ? image.cols - 32 : 0;
        const int yPx = corner >= 2 ? image.rows - 32 : 0;
        paintCell(image, xPx + 2, yPx + 2, 28, {0, 0, 0});
        paintCell(image, xPx + 6, yPx + 6, 20, {255, 255, 255});
        paintCell(image, xPx + 10, yPx + 10, 12, {0, 0, 0});
        if (corner == 1) { paintCell(image, xPx + 10, yPx + 10, 4, {255, 255, 255}); }
    }
}
// 输入：图片；输出：无；作用：生成上下两组 12x12 校准色块。
static void paintCalibration(cv::Mat& image)
{
    const auto palette = makePalette();
    for (int color = 0; color < 16; ++color)
    {
        paintCell(image, 48 + color * 12, 18, 12, palette[color]);
        paintCell(image, 48 + color * 12, image.rows - 24, 12, palette[color]);
    }
}
// 输入：头、负载；输出：图片；作用：生成完整版本 2 编码图。
cv::Mat renderFrame(const Header& header, const std::vector<uint8_t>& chunk)
{
    Logger::getInstance().write("INFO", "renderFrame() >> frame=" + std::to_string(header.frameIndex));
    const Layout& layout = header.layout;
    cv::Mat image(static_cast<int>(layout.heightPx), static_cast<int>(layout.widthPx), CV_8UC3, cv::Scalar(255, 255, 255));
    const auto palette = makePalette();
    paintMarkers(image);
    paintCalibration(image);
    ReedSolomon rs;
    const auto code = rs.encode(packHeader(header));
    for (size_t symbol = 0; symbol < HEADER_SYMBOLS; ++symbol)
    {
        const uint8_t value = static_cast<uint8_t>((code[symbol / 2] >> (symbol % 2 == 0 ? 4 : 0)) & 15);
        paintCell(image, BOOT_X_PX + static_cast<int>(symbol % BOOT_COLS) * BOOT_CELL_PX,
            BOOT_Y_PX + static_cast<int>(symbol / BOOT_COLS) * BOOT_CELL_PX, BOOT_CELL_PX, palette[value]);
    }
    const auto stream = encodePayload(chunk, rs);
    const auto starts = headerStarts(layout);
    size_t dataSymbol = 0;
    uint32_t random = 0x6d2b79f5U;
    // 每个主网格位置恰好赋值一次；未使用区域也按固定种子填满。
    for (size_t index = 0; index < layout.symbols; ++index)
    {
        uint8_t value = 0;
        bool isHeader = false;
        for (const size_t start : starts)
        {
            if (index >= start && index < start + HEADER_SYMBOLS)
            {
                const size_t offset = index - start;
                value = static_cast<uint8_t>((code[offset / 2] >> (offset % 2 == 0 ? 4 : 0)) & 15);
                isHeader = true;
                break;
            }
        }
        if (!isHeader)
        {
            if (dataSymbol < stream.size() * 2)
            {
                value = static_cast<uint8_t>((stream[dataSymbol / 2] >> (dataSymbol % 2 == 0 ? 4 : 0)) & 15);
            }
            else
            {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                value = static_cast<uint8_t>(random & 15);
            }
            ++dataSymbol;
        }
        paintCell(image, static_cast<int>(MARGIN_PX + index % layout.cols * layout.cellPx),
            static_cast<int>(MARGIN_PX + index / layout.cols * layout.cellPx), static_cast<int>(layout.cellPx), palette[value]);
    }
    Logger::getInstance().write("INFO", "renderFrame() << symbols=" + std::to_string(layout.symbols));
    return image;
}
// 输入：宽高；输出：有效尺寸；作用：在解压前统一限制内存规模。
static cv::Size checkedImageSize(uint32_t width, uint32_t height)
{
    if (width < 32 || height < 32 || width > MAX_SIDE_PX || height > MAX_SIDE_PX ||
        static_cast<uint64_t>(width) * height > MAX_PIXELS)
    {
        throw Failure(D2I_FORMAT, "Image dimensions unsupported (each side 32..16384, <=64M pixels)");
    }
    return cv::Size(static_cast<int>(width), static_cast<int>(height));
}
// 输入：PNG 字节；输出：尺寸；作用：检查固定 IHDR 长度与 8-bit 精度。
static cv::Size readPngSize(const std::vector<uint8_t>& bytes)
{
    const std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 33 || !std::equal(signature.begin(), signature.end(), bytes.begin()) ||
        std::memcmp(bytes.data() + 12, "IHDR", 4) != 0 || bytes[8] != 0 || bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 13)
    {
        throw Failure(D2I_FORMAT, "Not a valid PNG IHDR");
    }
    uint32_t width = 0;
    uint32_t height = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        width = (width << 8) | bytes[16 + i];
        height = (height << 8) | bytes[20 + i];
    }
    if (bytes[24] != 8)
    {
        throw Failure(D2I_FORMAT, "PNG sample precision must be 8-bit");
    }
    return checkedImageSize(width, height);
}
// 输入：JPEG 字节；输出：尺寸；作用：按标记段跳过元数据，在 SOF 处先检查解压尺寸。
static cv::Size readJpegSize(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
    {
        throw Failure(D2I_FORMAT, "Invalid JPEG SOI marker");
    }
    size_t offset = 2;
    // 只遍历扫描数据之前的标记段；每次推进有效段长，长度不足或 SOS/EOI 提前出现即失败。
    while (offset < bytes.size())
    {
        if (bytes[offset++] != 0xff) { throw Failure(D2I_FORMAT, "Invalid JPEG marker prefix"); }
        while (offset < bytes.size() && bytes[offset] == 0xff) { ++offset; }
        if (offset == bytes.size()) { throw Failure(D2I_FORMAT, "Truncated JPEG marker"); }
        const uint8_t marker = bytes[offset++];
        if (marker == 0x00 || marker == 0xd8 || marker == 0xda || marker == 0xd9)
        {
            throw Failure(D2I_FORMAT, "JPEG ended or reached scan data before a valid SOF header");
        }
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) { continue; }
        if (bytes.size() - offset < 2) { throw Failure(D2I_FORMAT, "Truncated JPEG segment length"); }
        const size_t length = (static_cast<size_t>(bytes[offset]) << 8) | bytes[offset + 1];
        if (length < 2 || length > bytes.size() - offset)
        {
            throw Failure(D2I_FORMAT, "Invalid or truncated JPEG segment");
        }
        const bool isSof = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (isSof)
        {
            if (marker != 0xc0 && marker != 0xc1 && marker != 0xc2)
            {
                throw Failure(D2I_FORMAT, "Unsupported JPEG coding process; use 8-bit sequential or progressive JPEG");
            }
            if (length < 8) { throw Failure(D2I_FORMAT, "Truncated JPEG SOF header"); }
            const uint8_t components = bytes[offset + 7];
            if (bytes[offset + 2] != 8 || components == 0 || components > 4 || length != 8 + 3 * static_cast<size_t>(components))
            {
                throw Failure(D2I_FORMAT, "Invalid JPEG SOF length, components or sample precision");
            }
            const uint32_t height = (static_cast<uint32_t>(bytes[offset + 3]) << 8) | bytes[offset + 4];
            const uint32_t width = (static_cast<uint32_t>(bytes[offset + 5]) << 8) | bytes[offset + 6];
            return checkedImageSize(width, height);
        }
        offset += length;
    }
    throw Failure(D2I_FORMAT, "JPEG SOF header not found");
}
// 输入：PNG/JPEG 路径；输出：BGR；作用：按文件内容识别格式，并在解压前验证尺寸。
cv::Mat loadImage(const std::filesystem::path& path)
{
    const auto bytes = readImageBytes(path);
    cv::Size size;
    const char* format = nullptr;
    if (bytes[0] == 0x89 && bytes[1] == 'P')
    {
        size = readPngSize(bytes);
        format = "PNG";
    }
    else if (bytes[0] == 0xff && bytes[1] == 0xd8)
    {
        size = readJpegSize(bytes);
        format = "JPEG";
    }
    else { throw Failure(D2I_FORMAT, "Unsupported image content; expected PNG or JPEG"); }
    Logger::getInstance().write("DEBUG", std::string("loadImage format=") + format + " width=" + std::to_string(size.width) + " height=" + std::to_string(size.height));
    Logger::getInstance().write("DEBUG", "cv::imdecode >> bytes=" + std::to_string(bytes.size()));
    cv::Mat image = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
    Logger::getInstance().write("DEBUG", "cv::imdecode << width=" + std::to_string(image.cols) + " height=" + std::to_string(image.rows));
    if (image.empty() || image.depth() != CV_8U || image.size() != size)
    {
        throw Failure(D2I_FORMAT, "Image decode failed or decoded dimensions/precision differ from header");
    }
    if (image.channels() == 4) { cv::cvtColor(image, image, cv::COLOR_BGRA2BGR); }
    if (image.channels() != 3) { throw Failure(D2I_FORMAT, "Image must contain RGB or RGBA color"); }
    return image;
}
// 输入：图片和路径；输出：无；作用：通过内存编码支持 Unicode 路径。
void saveImage(const std::filesystem::path& path, const cv::Mat& image)
{
    std::vector<uint8_t> bytes;
    Logger::getInstance().write("DEBUG", "cv::imencode >> PNG width=" + std::to_string(image.cols) + " height=" + std::to_string(image.rows));
    const bool result = cv::imencode(".png", image, bytes, {cv::IMWRITE_PNG_COMPRESSION, 3});
    Logger::getInstance().write("DEBUG", "cv::imencode << result=" + std::to_string(result) + " bytes=" + std::to_string(bytes.size()));
    if (!result) { throw Failure(D2I_IO, "PNG encoding failed"); }
    writeBytes(path, bytes);
}
// 输入：Lab 图片；输出：实测色中心；作用：用上下校准块抵消整帧色偏。
static std::array<cv::Vec3f, 16> readCenters(const cv::Mat& lab)
{
    std::array<cv::Vec3f, 16> centers{};
    for (int color = 0; color < 16; ++color)
    {
        const cv::Scalar top = cv::mean(lab(cv::Rect(51 + color * 12, 21, 6, 6)));
        // 局部引导定位时只有顶部 32 行；完整图像则合并两份校准条。
        cv::Scalar bottom = top;
        if (lab.rows >= 128) { bottom = cv::mean(lab(cv::Rect(51 + color * 12, lab.rows - 21, 6, 6))); }
        for (int channel = 0; channel < 3; ++channel)
        {
            centers[color][channel] = static_cast<float>((top[channel] + bottom[channel]) / 2);
        }
    }
    for (int left = 0; left < 16; ++left)
    {
        for (int right = left + 1; right < 16; ++right)
        {
            if (cv::norm(centers[left] - centers[right]) < 3)
            {
                throw Failure(D2I_FORMAT, "Calibration colors are indistinguishable");
            }
        }
    }
    return centers;
}
// 输入：Lab 样本、色中心；输出：符号；作用：按相同 Lab 标度计算最近邻和置信度。
static uint8_t classifySample(const cv::Vec3f& sample,
    const std::array<cv::Vec3f, 16>& centers, size_t& lowConfidence)
{
    float best = FLT_MAX;
    float second = FLT_MAX;
    uint8_t value = 0;
    for (uint8_t color = 0; color < 16; ++color)
    {
        const cv::Vec3f difference = sample - centers[color];
        const float distance = difference.dot(difference);
        if (distance < best) { second = best; best = distance; value = color; }
        else if (distance < second) { second = distance; }
    }
    if (second - best < 64) { ++lowConfidence; }
    return value;
}
// 输入：Lab 区域、色中心；输出：符号；作用：原生图像聚合整个色块的像素。
static uint8_t readSymbol(const cv::Mat& lab, int xPx, int yPx, int cellPx,
    const std::array<cv::Vec3f, 16>& centers, size_t& lowConfidence)
{
    cv::Vec3f sample(0, 0, 0);
    for (int y = yPx; y < yPx + cellPx; ++y)
    {
        const cv::Vec3b* row = lab.ptr<cv::Vec3b>(y);
        for (int x = xPx; x < xPx + cellPx; ++x)
        {
            for (int channel = 0; channel < 3; ++channel) { sample[channel] += row[x][channel]; }
        }
    }
    sample *= 1.0F / static_cast<float>(cellPx * cellPx);
    return classifySample(sample, centers, lowConfidence);
}
// 输入：图像、头部布局、RS；输出：有效头或空；作用：同时覆盖固定引导区与主网格副本。
static std::optional<Header> readHeader(const cv::Mat& lab, const Layout* layout, size_t start,
    const std::array<cv::Vec3f, 16>& centers, ReedSolomon& rs)
{
    std::array<uint8_t, RS_N> code{};
    size_t low = 0;
    for (size_t symbol = 0; symbol < HEADER_SYMBOLS; ++symbol)
    {
        const size_t index = start + symbol;
        const int xPx = layout ? static_cast<int>(MARGIN_PX + index % layout->cols * layout->cellPx)
                               : BOOT_X_PX + static_cast<int>(symbol % BOOT_COLS) * BOOT_CELL_PX;
        const int yPx = layout ? static_cast<int>(MARGIN_PX + index / layout->cols * layout->cellPx)
                               : BOOT_Y_PX + static_cast<int>(symbol / BOOT_COLS) * BOOT_CELL_PX;
        const uint8_t value = readSymbol(lab, xPx, yPx, layout ? static_cast<int>(layout->cellPx) : BOOT_CELL_PX, centers, low);
        code[symbol / 2] |= static_cast<uint8_t>(value << (symbol % 2 == 0 ? 4 : 0));
    }
    std::array<uint8_t, RS_K> bytes{};
    if (!rs.decode(code, bytes)) { return std::nullopt; }
    try { return unpackHeader(bytes); }
    catch (const Failure&) { return std::nullopt; }
}
// 输入：图片；输出：定位图样是否匹配；作用：原生读取前验证四角及方向。
static bool hasMarkers(const cv::Mat& image)
{
    cv::Mat expected(64, 64, CV_8UC3, cv::Scalar(255, 255, 255));
    paintMarkers(expected);
    for (int corner = 0; corner < 4; ++corner)
    {
        const int xPx = (corner == 1 || corner == 2) ? image.cols - 32 : 0;
        const int yPx = corner >= 2 ? image.rows - 32 : 0;
        const int refX = (corner == 1 || corner == 2) ? 32 : 0;
        const int refY = corner >= 2 ? 32 : 0;
        size_t wrong = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const auto got = image.at<cv::Vec3b>(yPx + y, xPx + x);
                const int intensity = (got[0] + got[1] + got[2]) / 3;
                const bool dark = expected.at<cv::Vec3b>(refY + y, refX + x)[0] == 0;
                if ((dark && intensity > 128) || (!dark && intensity < 128)) { ++wrong; }
            }
        }
        if (wrong > 190) { return false; }
        // 同心方块本身对旋转对称，必须单独核对右上角的白色方向角点。
        const auto direction = image.at<cv::Vec3b>(yPx + 11, xPx + 11);
        const int brightness = (direction[0] + direction[1] + direction[2]) / 3;
        if ((corner == 1 && brightness < 128) || (corner != 1 && brightness > 128)) { return false; }
    }
    return true;
}
// 输入：已规整图片；输出：有效帧；作用：直接读取，正常路径不进行重采样。
static ScannedFrame scanAligned(const cv::Mat& image)
{
    if (image.cols < 384 || image.rows < 128 || !hasMarkers(image))
    {
        throw Failure(D2I_FORMAT, "Corner markers or orientation invalid");
    }
    cv::Mat lab;
    cv::cvtColor(image, lab, cv::COLOR_BGR2Lab);
    const auto centers = readCenters(lab);
    ReedSolomon rs;
    std::optional<Header> chosen = readHeader(lab, nullptr, 0, centers, rs);
    if (chosen && (chosen->layout.widthPx != static_cast<uint32_t>(image.cols) ||
        chosen->layout.heightPx != static_cast<uint32_t>(image.rows)))
    {
        throw Failure(D2I_FORMAT, "Bootstrap dimensions do not match image dimensions");
    }
    Layout layout;
    if (chosen) { layout = chosen->layout; }
    else
    {
        // 引导头损坏时，以有限的 1..32 边长候选恢复主网格头，不要求调用者提供参数。
        for (uint32_t cell = 1; cell <= MAX_CELL_PX && !chosen; ++cell)
        {
            try { layout = makeLayout(image.cols, image.rows, cell); }
            catch (const Failure&) { continue; }
            for (const size_t start : headerStarts(layout))
            {
                const auto candidate = readHeader(lab, &layout, start, centers, rs);
                if (candidate && candidate->layout.widthPx == static_cast<uint32_t>(image.cols) &&
                    candidate->layout.heightPx == static_cast<uint32_t>(image.rows) && candidate->layout.cellPx == cell)
                {
                    chosen = candidate;
                    break;
                }
            }
        }
    }
    if (!chosen) { throw Failure(D2I_FORMAT, "No valid bootstrap or grid header"); }
    layout = chosen->layout;
    ScannedFrame result;
    result.header = *chosen;
    const auto expected = packHeader(*chosen);
    for (const size_t start : headerStarts(layout))
    {
        const auto candidate = readHeader(lab, &layout, start, centers, rs);
        if (!candidate) { continue; }
        if (packHeader(*candidate) != expected)
        {
            throw Failure(D2I_FORMAT, "Valid header copies contradict each other");
        }
        ++result.validHeaders;
    }
    std::vector<uint8_t> stream(static_cast<size_t>(chosen->blocks) * RS_N, 0);
    size_t dataSymbol = 0;
    size_t low = 0;
    for (size_t index = 0; index < layout.symbols && dataSymbol < stream.size() * 2; ++index)
    {
        if (isHeaderSymbol(index, layout)) { continue; }
        const auto value = readSymbol(lab, static_cast<int>(MARGIN_PX + index % layout.cols * layout.cellPx),
            static_cast<int>(MARGIN_PX + index / layout.cols * layout.cellPx), static_cast<int>(layout.cellPx), centers, low);
        stream[dataSymbol / 2] |= static_cast<uint8_t>(value << (dataSymbol % 2 == 0 ? 4 : 0));
        ++dataSymbol;
    }
    result.chunk = decodePayload(stream, *chosen, rs);
    Logger::getInstance().write("INFO", "frame=" + std::to_string(chosen->frameIndex) + " header_valid_copies=" +
        std::to_string(result.validHeaders) + " low_confidence_symbols=" + std::to_string(low) + " rs_failed_blocks=0 crc_pass=true");
    return result;
}

// 嵌套同心轮廓候选。
struct Marker
{
    std::array<cv::Point2f, 4> points; // 沿轮廓顺序排列的四角。
    cv::Point2f center; // 外黑环中心。
    double area = 0; // 轮廓面积，单位像素平方。
};
// 输入：两候选；输出：面积降序；作用：优先处理完整定位块。
static bool hasLargerArea(const Marker& left, const Marker& right) { return left.area > right.area; }
// 输入：截图；输出：有界候选列表；作用：通过嵌套环和几何比例定位。
static std::vector<Marker> findMarkers(const cv::Mat& image)
{
    cv::Mat gray;
    cv::Mat binary;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
    std::vector<Marker> result;
    for (size_t index = 0; index < contours.size(); ++index)
    {
        const int child = hierarchy[index][2];
        if (child < 0 || hierarchy[child][2] < 0) { continue; }
        const double area = std::abs(cv::contourArea(contours[index]));
        const double inner = std::abs(cv::contourArea(contours[child]));
        if (area < 49 || inner / area < 0.3 || inner / area > 0.9) { continue; }
        std::vector<cv::Point> quad;
        cv::approxPolyDP(contours[index], quad, cv::arcLength(contours[index], true) * 0.025, true);
        if (quad.size() != 4 || !cv::isContourConvex(quad)) { continue; }
        const cv::Rect box = cv::boundingRect(quad);
        const double ratio = static_cast<double>(box.width) / box.height;
        if (ratio < 0.65 || ratio > 1.55) { continue; }
        Marker marker;
        marker.area = area;
        // findContours 的环向可能为逆时针，使用有符号面积统一为顺时针。
        if (cv::contourArea(quad, true) < 0) { std::reverse(quad.begin(), quad.end()); }
        for (size_t p = 0; p < 4; ++p)
        {
            marker.points[p] = cv::Point2f(quad[p]);
            marker.center += marker.points[p] * 0.25F;
        }
        result.push_back(marker);
    }
    std::sort(result.begin(), result.end(), hasLargerArea);
    if (result.size() > 256) { result.resize(256); }
    return result;
}
// 输入：Lab 截图、局部映射；输出：有效引导头；作用：直接在截图坐标采样中心，避免微小色块二次混色。
static std::optional<Header> sampleBootstrap(const cv::Mat& lab, const cv::Mat& mapping)
{
    std::vector<cv::Point2f> points;
    for (size_t color = 0; color < 16; ++color) { points.emplace_back(53.5F + color * 12, 23.5F); }
    for (size_t symbol = 0; symbol < HEADER_SYMBOLS; ++symbol)
    {
        points.emplace_back(48.5F + static_cast<float>(symbol % BOOT_COLS) * 2,
            4.5F + static_cast<float>(symbol / BOOT_COLS) * 2);
    }
    std::vector<cv::Point2f> observed;
    cv::perspectiveTransform(points, observed, mapping);
    std::array<cv::Vec3f, 16> centers{};
    for (size_t color = 0; color < 16; ++color)
    {
        const int x = cvRound(observed[color].x);
        const int y = cvRound(observed[color].y);
        if (x < 1 || y < 1 || x >= lab.cols - 1 || y >= lab.rows - 1) { return std::nullopt; }
        const auto value = cv::mean(lab(cv::Rect(x - 1, y - 1, 3, 3)));
        centers[color] = cv::Vec3f(static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
    }
    if (cv::norm(centers[0] - centers[1]) < 80) { return std::nullopt; }
    for (size_t left = 0; left < 16; ++left)
    {
        for (size_t right = left + 1; right < 16; ++right)
        {
            if (cv::norm(centers[left] - centers[right]) < 3) { return std::nullopt; }
        }
    }
    std::array<uint8_t, RS_N> code{};
    size_t low = 0;
    for (size_t symbol = 0; symbol < HEADER_SYMBOLS; ++symbol)
    {
        const int x = cvRound(observed[16 + symbol].x);
        const int y = cvRound(observed[16 + symbol].y);
        if (x < 0 || y < 0 || x >= lab.cols || y >= lab.rows) { return std::nullopt; }
        const auto pixel = lab.at<cv::Vec3b>(y, x);
        const auto value = classifySample(cv::Vec3f(pixel[0], pixel[1], pixel[2]), centers, low);
        code[symbol / 2] |= static_cast<uint8_t>(value << (symbol % 2 == 0 ? 4 : 0));
    }
    ReedSolomon rs;
    std::array<uint8_t, RS_K> bytes{};
    if (!rs.decode(code, bytes)) { return std::nullopt; }
    try
    {
        return unpackHeader(bytes);
    }
    catch (const Failure&) { return std::nullopt; }
}
// 输入：截图、方向确定的定位环；输出：引导头和映射；作用：补偿缩放后轮廓像素量化误差。
static std::optional<Header> locateBootstrap(const cv::Mat& image,
    const std::array<cv::Point2f, 4>& observed, cv::Mat& mapping)
{
    auto header = sampleBootstrap(image, mapping);
    if (header) { return header; }
    const cv::Point2f right = (observed[1] - observed[0]) * (1.0F / 27.0F);
    const cv::Point2f down = (observed[3] - observed[0]) * (1.0F / 27.0F);
    const cv::Point2f center = (observed[0] + observed[1] + observed[2] + observed[3]) * 0.25F;
    // 小定位环经缩放后只有十几个像素，其边界量化会放大到引导区；有限搜索局部比例和相位。
    for (int step = 0; step <= 40; ++step)
    {
        const int signedStep = step % 2 == 0 ? step / 2 : -(step + 1) / 2;
        const double factor = 1.0 + signedStep * 0.005;
        for (const float shiftY : {0.0F, -0.5F, 0.5F})
        {
            for (const float shiftX : {0.0F, -0.5F, 0.5F})
            {
                const cv::Point2f offset = center + right * shiftX + down * shiftY;
                cv::Mat candidate = (cv::Mat_<double>(3, 3) <<
                    right.x * factor, down.x * factor, offset.x - 15.5 * (right.x + down.x) * factor,
                    right.y * factor, down.y * factor, offset.y - 15.5 * (right.y + down.y) * factor,
                    0, 0, 1);
                header = sampleBootstrap(image, candidate);
                if (header) { mapping = candidate; return header; }
            }
        }
    }
    return std::nullopt;
}
// 输入：Lab 原截图、映射、原生坐标；输出：颜色；作用：在原截图上直接取样，不重复缩放。
static cv::Vec3f mappedPixel(const cv::Mat& lab, const cv::Mat& mapping, float xPx, float yPx)
{
    const double* row0 = mapping.ptr<double>(0);
    const double* row1 = mapping.ptr<double>(1);
    const double* row2 = mapping.ptr<double>(2);
    const double divisor = row2[0] * xPx + row2[1] * yPx + row2[2];
    if (std::abs(divisor) < 1e-12) { throw Failure(D2I_FORMAT, "Degenerate screenshot mapping"); }
    const int x = cvRound((row0[0] * xPx + row0[1] * yPx + row0[2]) / divisor);
    const int y = cvRound((row1[0] * xPx + row1[1] * yPx + row1[2]) / divisor);
    if (x < 0 || y < 0 || x >= lab.cols || y >= lab.rows)
    {
        throw Failure(D2I_FORMAT, "Mapped grid extends outside screenshot");
    }
    const auto pixel = lab.at<cv::Vec3b>(y, x);
    return cv::Vec3f(pixel[0], pixel[1], pixel[2]);
}
// 输入：截图、四角映射、有效引导头；输出：帧；作用：映射色块中心，避免缩放后边界混色。
static ScannedFrame scanMappedFrame(const cv::Mat& lab, const cv::Mat& mapping, const Header& bootstrap)
{
    const Layout& layout = bootstrap.layout;
    std::array<cv::Vec3f, 16> centers{};
    for (size_t color = 0; color < 16; ++color)
    {
        const float x = 53.5F + static_cast<float>(color) * 12;
        centers[color] = (mappedPixel(lab, mapping, x, 23.5F) +
            mappedPixel(lab, mapping, x, layout.heightPx - 18.5F)) * 0.5F;
    }
    ReedSolomon rs;
    ScannedFrame result;
    result.header = bootstrap;
    size_t low = 0;
    const float middle = (layout.cellPx - 1) * 0.5F;
    for (const size_t start : headerStarts(layout))
    {
        std::array<uint8_t, RS_N> code{};
        for (size_t symbol = 0; symbol < HEADER_SYMBOLS; ++symbol)
        {
            const size_t index = start + symbol;
            const auto pixel = mappedPixel(lab, mapping,
                static_cast<float>(32 + index % layout.cols * layout.cellPx) + middle,
                static_cast<float>(32 + index / layout.cols * layout.cellPx) + middle);
            code[symbol / 2] |= static_cast<uint8_t>(classifySample(pixel, centers, low) << (symbol % 2 == 0 ? 4 : 0));
        }
        std::array<uint8_t, RS_K> bytes{};
        if (!rs.decode(code, bytes)) { continue; }
        std::optional<Header> candidate;
        try { candidate = unpackHeader(bytes); } catch (const Failure&) { continue; }
        if (packHeader(*candidate) != packHeader(bootstrap))
        {
            throw Failure(D2I_FORMAT, "Valid mapped header contradicts bootstrap");
        }
        ++result.validHeaders;
    }
    std::vector<uint8_t> stream(static_cast<size_t>(bootstrap.blocks) * RS_N, 0);
    size_t symbol = 0;
    for (size_t index = 0; index < layout.symbols && symbol < stream.size() * 2; ++index)
    {
        if (isHeaderSymbol(index, layout)) { continue; }
        const auto pixel = mappedPixel(lab, mapping,
            static_cast<float>(32 + index % layout.cols * layout.cellPx) + middle,
            static_cast<float>(32 + index / layout.cols * layout.cellPx) + middle);
        stream[symbol / 2] |= static_cast<uint8_t>(classifySample(pixel, centers, low) << (symbol % 2 == 0 ? 4 : 0));
        ++symbol;
    }
    result.chunk = decodePayload(stream, bootstrap, rs);
    Logger::getInstance().write("INFO", "frame=" + std::to_string(bootstrap.frameIndex) + " header_valid_copies=" +
        std::to_string(result.validHeaders) + " low_confidence_symbols=" + std::to_string(low) + " rs_failed_blocks=0 crc_pass=true");
    return result;
}
// 输入：截图；输出：有效帧；作用：先局部引导，再通过四角几何规整完整画布。
static ScannedFrame scanFallback(const cv::Mat& image)
{
    const auto markers = findMarkers(image);
    cv::Mat sourceLab;
    cv::cvtColor(image, sourceLab, cv::COLOR_BGR2Lab);
    const std::array<cv::Point2f, 4> local{{{2, 2}, {29, 2}, {29, 29}, {2, 29}}};
    std::string lastError = "No decodable bootstrap near detected corner markers";
    // 每个候选最多尝试四个旋转方向；固定标记尺度使引导头读取不依赖未知色块尺寸。
    for (const auto& marker : markers)
    {
        for (size_t rotation = 0; rotation < 4; ++rotation)
        {
            std::array<cv::Point2f, 4> observed{};
            for (size_t point = 0; point < 4; ++point) { observed[point] = marker.points[(point + rotation) % 4]; }
            cv::Mat mapping = cv::getPerspectiveTransform(local.data(), observed.data());
            try
            {
                const auto bootstrap = locateBootstrap(sourceLab, observed, mapping);
                if (!bootstrap) { continue; }
                const Layout& layout = bootstrap->layout;
                std::vector<cv::Point2f> native{
                    {15.5F, 15.5F}, {layout.widthPx - 16.5F, 15.5F},
                    {layout.widthPx - 16.5F, layout.heightPx - 16.5F}, {15.5F, layout.heightPx - 16.5F}};
                std::vector<cv::Point2f> predicted;
                cv::perspectiveTransform(native, predicted, mapping);
                std::vector<cv::Point2f> actual;
                std::set<size_t> used;
                for (const auto& position : predicted)
                {
                    double best = DBL_MAX;
                    size_t choice = 0;
                    for (size_t candidate = 0; candidate < markers.size(); ++candidate)
                    {
                        const double distance = cv::norm(position - markers[candidate].center);
                        if (distance < best) { best = distance; choice = candidate; }
                    }
                    if (best > (std::max)(image.cols, image.rows) * 0.15 || !used.insert(choice).second)
                    {
                        throw Failure(D2I_FORMAT, "Cannot match four distinct corner markers");
                    }
                    actual.push_back(markers[choice].center);
                }
                const cv::Mat fullMap = cv::getPerspectiveTransform(native, actual);
                auto frame = scanMappedFrame(sourceLab, fullMap, *bootstrap);
                if (packHeader(frame.header) != packHeader(*bootstrap))
                {
                    throw Failure(D2I_FORMAT, "Localized header contradicts decoded frame header");
                }
                Logger::getInstance().write("INFO", "geometry_found=true fallback=true rescale_ratio=" +
                    std::to_string(cv::norm(actual[1] - actual[0]) / (layout.widthPx - 32.0)));
                return frame;
            }
            catch (const Failure& error) { lastError = error.what(); }
        }
    }
    throw Failure(D2I_FORMAT, "Fallback failed; screenshot resolution/geometry may be insufficient: " + lastError);
}
// 输入：路径；输出：有效帧；作用：优先无重采样路径，几何失败才尝试定位。
ScannedFrame scanFrame(const std::filesystem::path& path)
{
    Logger::getInstance().write("INFO", "scanFrame() >> " + toUtf8(path.wstring()));
    try
    {
        const cv::Mat image = loadImage(path);
        try
        {
            auto frame = scanAligned(image);
            Logger::getInstance().write("INFO", "scanFrame() << geometry_found=true fallback=false");
            return frame;
        }
        catch (const Failure& error)
        {
            // 已正确定位的图像出现纠错或副本冲突时，保留原错误，不以重采样掩盖。
            if (error.getCode() == D2I_CORRECTION || hasMarkers(image)) { throw; }
            Logger::getInstance().write("DEBUG", std::string("Fast path unavailable: ") + error.what());
        }
        auto frame = scanFallback(image);
        Logger::getInstance().write("INFO", "scanFrame() << fallback=true crc_pass=true");
        return frame;
    }
    catch (const Failure& error)
    {
        Logger::getInstance().write("WARN", std::string("scanFrame() << ") + error.what());
        throw;
    }
}
}
