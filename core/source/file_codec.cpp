#include "file_codec.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>

namespace d2i
{
// 输入：路径；输出：是否为支持的图片；作用：扩展名不区分大小写。
static bool isSupportedImage(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    for (auto& value : extension) { value = static_cast<wchar_t>(std::towlower(value)); }
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg";
}
// 输入：目录、帧索引；输出：文件名；作用：人类编号从 1 开始。
static std::filesystem::path framePath(const std::filesystem::path& folder, uint32_t index)
{
    std::wostringstream name;
    name << L"frame_" << std::setfill(L'0') << std::setw(6) << static_cast<uint64_t>(index) + 1 << L".png";
    return folder / name.str();
}
// 输入：两路径；输出：是否相同文件；作用：保护输入文件及硬链接。
static bool isSamePath(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code error;
    if (std::filesystem::exists(left) && std::filesystem::exists(right))
    {
        const bool same = std::filesystem::equivalent(left, right, error);
        if (!error && same) { return true; }
    }
    return std::filesystem::weakly_canonical(left) == std::filesystem::weakly_canonical(right);
}
// 输入：头、开始时间；输出：API 结果；作用：统一成功结果填充。
static D2iReport makeReport(const Header& header, std::chrono::steady_clock::time_point started)
{
    D2iReport report{};
    report.structBytes = sizeof(report);
    report.fileBytes = header.fileSize;
    report.frameCount = header.frameCount;
    report.capacityBytes = header.layout.capacity;
    std::copy(header.sha.begin(), header.sha.end(), report.sha256);
    report.elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    return report;
}
// 输入：配置；输出：结果；作用：两遍流式处理，检测编码期间输入变化。
D2iReport encodeFile(const D2iEncodeOptions& options)
{
    Logger::getInstance().write("INFO", "encodeFile() >> input=" + toUtf8(options.inputPath));
    const auto started = std::chrono::steady_clock::now();
    const Layout layout = makeLayout(options.widthPx, options.heightPx, options.cellPx);
    const std::filesystem::path input(options.inputPath);
    const std::filesystem::path folder(options.outputDir);
    if (!std::filesystem::is_regular_file(input)) { throw Failure(D2I_IO, "Input is not a regular file"); }
    Header header;
    header.layout = layout;
    header.fileSize = std::filesystem::file_size(input);
    const uint64_t count = header.fileSize == 0 ? 1 : 1 + (header.fileSize - 1) / layout.capacity;
    if (count > UINT32_MAX || header.fileSize > static_cast<uint64_t>(INT64_MAX))
    {
        throw Failure(D2I_ARGUMENT, "File exceeds format frame-count or stream-offset range");
    }
    header.frameCount = static_cast<uint32_t>(count);
    header.sha = hashFile(input);
    std::filesystem::create_directories(folder);
    // 所有既有输出预先检查，避免正常的覆盖拒绝发生在中途。
    for (uint32_t index = 0; index < header.frameCount; ++index)
    {
        const auto target = framePath(folder, index);
        if (isSamePath(input, target)) { throw Failure(D2I_IO, "Output would replace input file"); }
        if (std::filesystem::exists(target) && !options.overwrite)
        {
            throw Failure(D2I_IO, "Output exists; use --overwrite: " + toUtf8(target.wstring()));
        }
    }
    std::ifstream in(input, std::ios::binary);
    if (!in) { throw Failure(D2I_IO, "Cannot reopen input"); }
    Hasher secondHash;
    for (uint32_t index = 0; index < header.frameCount; ++index)
    {
        header.frameIndex = index;
        header.chunkOffset = static_cast<uint64_t>(index) * layout.capacity;
        header.chunkSize = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(layout.capacity), header.fileSize - header.chunkOffset));
        header.blocks = static_cast<uint32_t>((header.chunkSize + RS_K - 1) / RS_K);
        std::vector<uint8_t> chunk(header.chunkSize);
        if (!chunk.empty() && !in.read(reinterpret_cast<char*>(chunk.data()), chunk.size()))
        {
            throw Failure(D2I_IO, "Input changed or became unreadable during encoding");
        }
        secondHash.append(chunk.data(), chunk.size());
        header.chunkCrc = calculateCrc(chunk.data(), chunk.size());
        const auto target = framePath(folder, index);
        StagedFile staged(target, options.overwrite != 0);
        saveImage(staged.getPath(), renderFrame(header, chunk));
        staged.commit();
        Logger::getInstance().write("INFO", "encoded=" + toUtf8(target.wstring()) + " payload_bytes=" +
            std::to_string(chunk.size()) + " png_bytes=" + std::to_string(std::filesystem::file_size(target)) +
            " width=" + std::to_string(layout.widthPx) + " height=" + std::to_string(layout.heightPx));
    }
    // 额外字节和最终摘要共同检查，避免文件变化时返回伪成功。
    if (in.peek() != std::char_traits<char>::eof() || secondHash.finish() != header.sha ||
        std::filesystem::file_size(input) != header.fileSize)
    {
        throw Failure(D2I_IO, "Input changed during encoding; discard this incomplete output set");
    }
    Logger::getInstance().write("INFO", "encodeFile() << frames=" + std::to_string(header.frameCount));
    return makeReport(header, started);
}
// 输入：配置；输出：唯一图片列表；作用：支持 PNG/JPEG 混合目录、重复路径和递归选择。
static std::vector<std::filesystem::path> collectImages(const D2iDecodeOptions& options)
{
    std::vector<std::filesystem::path> files;
    for (uint32_t index = 0; index < options.inputCount; ++index)
    {
        if (!options.inputs[index] || options.inputs[index][0] == L'\0')
        {
            throw Failure(D2I_ARGUMENT, "Empty decode input path");
        }
        const std::filesystem::path input(options.inputs[index]);
        if (std::filesystem::is_directory(input))
        {
            if (options.recursive)
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(input))
                {
                    if (entry.is_regular_file() && isSupportedImage(entry.path())) { files.push_back(std::filesystem::canonical(entry.path())); }
                }
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(input))
                {
                    if (entry.is_regular_file() && isSupportedImage(entry.path())) { files.push_back(std::filesystem::canonical(entry.path())); }
                }
            }
        }
        else if (std::filesystem::is_regular_file(input) && isSupportedImage(input))
        {
            files.push_back(std::filesystem::canonical(input));
        }
        else { throw Failure(D2I_IO, "Input missing or not PNG/JPG/JPEG/directory: " + toUtf8(input.wstring())); }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    if (files.empty()) { throw Failure(D2I_IO, "No PNG/JPG/JPEG input files found"); }
    return files;
}
// 输入：已恢复序号、总帧数；输出：缺失区间；作用：不遍历恶意的巨大 frame_count。
static std::string missingFrames(const std::map<uint32_t, Header>& frames, uint32_t total)
{
    uint64_t next = 0;
    size_t gaps = 0;
    std::ostringstream message;
    for (const auto& frame : frames)
    {
        if (frame.first > next)
        {
            if (gaps < 32) { message << ' ' << next << ".." << frame.first - 1; }
            ++gaps;
        }
        next = static_cast<uint64_t>(frame.first) + 1;
    }
    if (next < total) { message << ' ' << next << ".." << total - 1; }
    if (gaps > 32) { message << " (more ranges omitted)"; }
    return message.str();
}
// 输入：配置；输出：结果；作用：只缓存帧元数据，负载写入同目录暂存文件。
D2iReport decodeFiles(const D2iDecodeOptions& options)
{
    Logger::getInstance().write("INFO", "decodeFiles() >> output=" + toUtf8(options.outputPath));
    const auto started = std::chrono::steady_clock::now();
    const auto files = collectImages(options);
    const std::filesystem::path output(options.outputPath);
    for (const auto& path : files)
    {
        if (isSamePath(path, output)) { throw Failure(D2I_IO, "Output would replace an input image"); }
    }
    std::string selected = options.fileId ? options.fileId : "";
    for (auto& character : selected)
    {
        if (character >= 'A' && character <= 'F') { character = static_cast<char>(character - 'A' + 'a'); }
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
        {
            throw Failure(D2I_ARGUMENT, "--file-id must contain 32 hexadecimal characters");
        }
    }
    if (!selected.empty() && selected.size() != 32) { throw Failure(D2I_ARGUMENT, "--file-id must contain 32 hexadecimal characters"); }
    StagedFile staged(output, options.overwrite != 0);
    std::fstream out(staged.getPath(), std::ios::binary | std::ios::in | std::ios::out);
    if (!out) { throw Failure(D2I_IO, "Cannot open temporary output"); }
    std::map<uint32_t, Header> frames;
    std::optional<Header> group;
    int lastCode = D2I_FORMAT;
    std::string lastError = "No valid frames found";
    for (const auto& path : files)
    {
        ScannedFrame frame;
        try { frame = scanFrame(path); }
        catch (const Failure& error)
        {
            lastCode = error.getCode();
            lastError = error.what();
            Logger::getInstance().write("WARN", "Skipped image=" + toUtf8(path.wstring()) + " reason=" + error.what());
            continue;
        }
        catch (const cv::Exception& error)
        {
            lastCode = D2I_FORMAT;
            lastError = error.what();
            Logger::getInstance().write("WARN", "Skipped image=" + toUtf8(path.wstring()) + " OpenCV=" + error.what());
            continue;
        }
        const Header& header = frame.header;
        if (!selected.empty() && selected != toHex(header.sha.data(), 16))
        {
            Logger::getInstance().write("INFO", "Skipped other file ID=" + toHex(header.sha.data(), 16));
            continue;
        }
        if (!group) { group = header; }
        if (!isSameFile(*group, header))
        {
            throw Failure(D2I_FRAME_SET, "Mixed valid files or layouts; use --file-id to select one file");
        }
        const auto existing = frames.find(header.frameIndex);
        if (existing != frames.end())
        {
            if (packHeader(existing->second) != packHeader(header))
            {
                throw Failure(D2I_FRAME_SET, "Duplicate frame has contradictory metadata");
            }
            std::vector<uint8_t> previous(header.chunkSize);
            out.seekg(static_cast<std::streamoff>(header.chunkOffset));
            if (!previous.empty()) { out.read(reinterpret_cast<char*>(previous.data()), previous.size()); }
            if (!out || previous != frame.chunk) { throw Failure(D2I_FRAME_SET, "Duplicate frame content differs"); }
            Logger::getInstance().write("INFO", "Duplicate frame accepted index=" + std::to_string(header.frameIndex));
            continue;
        }
        out.seekp(static_cast<std::streamoff>(header.chunkOffset));
        if (!frame.chunk.empty()) { out.write(reinterpret_cast<const char*>(frame.chunk.data()), frame.chunk.size()); }
        out.flush();
        if (!out) { throw Failure(D2I_IO, "Writing temporary file failed (check free disk space)"); }
        frames.emplace(header.frameIndex, header);
    }
    if (!group)
    {
        if (!selected.empty()) { throw Failure(D2I_FRAME_SET, "No valid frames matched --file-id=" + selected + "; last diagnostic: " + lastError); }
        throw Failure(lastCode, "No selected valid frames; last diagnostic: " + lastError);
    }
    if (frames.size() != group->frameCount)
    {
        throw Failure(D2I_FRAME_SET, "Missing frame indices (0-based):" + missingFrames(frames, group->frameCount));
    }
    out.close();
    if (!out) { throw Failure(D2I_IO, "Closing temporary output failed"); }
    if (std::filesystem::file_size(staged.getPath()) != group->fileSize || hashFile(staged.getPath()) != group->sha)
    {
        throw Failure(D2I_HASH, "Final file SHA256/size mismatch; output was not committed");
    }
    staged.commit();
    Logger::getInstance().write("INFO", "decodeFiles() << sha256_pass=true frames=" + std::to_string(frames.size()));
    return makeReport(*group, started);
}
}
