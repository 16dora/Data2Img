#include "cli.h"
#include "data2img.h"
#include <Windows.h>
#include <array>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// 输入：无；输出：终端帮助；作用：只公开本阶段的两项功能。
static void printHelp()
{
    std::cout <<
        "Data2Img CLI (core DLL, format 2)\n"
        "  screen_codec encode -i FILE -o DIR [--width 3840] [--height 2060] [--cell 2]\n"
        "                      [--overwrite] [--verbose]\n"
        "  screen_codec decode -i IMAGE_OR_DIR [IMAGE_OR_DIR ...] -o FILE\n"
        "                      [--recursive] [--file-id HEX32] [--overwrite] [--verbose]\n"
        "Layout: width 384..16384, height 128..16384, <=64M pixels, cell 1..32.\n"
        "The grid must fit three headers and at least one data RS block.\n"
        "16 colors, RS(255,191), CRC32 + SHA256; decoder reads layout automatically.\n"
        "Decode accepts PNG, JPG and JPEG; JPEG recovery depends on image quality.\n"
        "All paths support Windows Unicode. Existing outputs require --overwrite.\n";
}
// 输入：参数文本；输出：整数；作用：拒绝符号、溢出和非数字尾缀。
static uint32_t parseNumber(const std::wstring& value)
{
    if (value.empty()) { throw std::invalid_argument("Empty numeric option"); }
    uint64_t result = 0;
    for (const wchar_t character : value)
    {
        if (character < L'0' || character > L'9') { throw std::invalid_argument("Numeric options require unsigned decimal integers"); }
        result = result * 10 + character - L'0';
        if (result > UINT32_MAX) { throw std::invalid_argument("Numeric option exceeds uint32 range"); }
    }
    return static_cast<uint32_t>(result);
}
// 输入：结果；输出：终端文本；作用：显示校验成功后的必要信息。
static void printReport(const D2iReport& report)
{
    std::cout << "file_bytes=" << report.fileBytes << " frames=" << report.frameCount
              << " capacity_bytes=" << report.capacityBytes << " elapsed_ms=" << report.elapsedMs << "\nsha256=";
    std::cout << std::hex << std::setfill('0');
    for (const uint8_t value : report.sha256) { std::cout << std::setw(2) << static_cast<unsigned>(value); }
    std::cout << std::dec << '\n';
}
// 输入：参数；输出：状态；作用：将命令行转换为 DLL 调用参数。
int runCli(int argc, wchar_t* argv[])
{
    try
    {
        if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"-h")
        {
            printHelp();
            return argc < 2 ? D2I_ARGUMENT : D2I_OK;
        }
        const std::wstring command(argv[1]);
        if (command != L"encode" && command != L"decode") { throw std::invalid_argument("Expected encode or decode"); }
        std::vector<std::wstring> inputs;
        std::wstring output;
        std::string fileId;
        D2iEncodeOptions encode{};
        encode.structBytes = sizeof(encode);
        encode.widthPx = 3840;
        encode.heightPx = 2060;
        encode.cellPx = 2;
        D2iDecodeOptions decode{};
        decode.structBytes = sizeof(decode);
        for (int index = 2; index < argc; ++index)
        {
            const std::wstring option(argv[index]);
            if (option == L"--help" || option == L"-h") { printHelp(); return D2I_OK; }
            if (option == L"--verbose")
            {
                if (d2i_init_logging(1) != D2I_OK) { throw std::runtime_error("Cannot enable verbose logging"); }
                continue;
            }
            if (option == L"--overwrite") { encode.overwrite = 1; decode.overwrite = 1; continue; }
            if (option == L"--recursive" && command == L"decode") { decode.recursive = 1; continue; }
            if (option == L"-i")
            {
                const size_t before = inputs.size();
                while (index + 1 < argc && argv[index + 1][0] != L'-') { inputs.emplace_back(argv[++index]); }
                if (inputs.size() == before) { throw std::invalid_argument("-i requires at least one path"); }
                continue;
            }
            if (index + 1 >= argc) { throw std::invalid_argument("Missing option value"); }
            const std::wstring value(argv[++index]);
            if (option == L"-o") { output = value; }
            else if (option == L"--width" && command == L"encode") { encode.widthPx = parseNumber(value); }
            else if (option == L"--height" && command == L"encode") { encode.heightPx = parseNumber(value); }
            else if (option == L"--cell" && command == L"encode") { encode.cellPx = parseNumber(value); }
            else if (option == L"--file-id" && command == L"decode")
            {
                fileId.clear();
                for (const auto character : value)
                {
                    if (character > 127) { throw std::invalid_argument("--file-id must be ASCII hex"); }
                    fileId.push_back(static_cast<char>(character));
                }
            }
            else { throw std::invalid_argument("Unknown option or option not supported for this command"); }
        }
        if (inputs.empty() || output.empty()) { throw std::invalid_argument("Both -i and -o are required"); }
        D2iReport report{};
        report.structBytes = sizeof(report);
        std::array<char, 4096> error{};
        int status = D2I_ARGUMENT;
        if (command == L"encode")
        {
            if (inputs.size() != 1) { throw std::invalid_argument("encode accepts exactly one input file"); }
            encode.inputPath = inputs[0].c_str();
            encode.outputDir = output.c_str();
            status = d2i_encode(&encode, &report, error.data(), error.size());
        }
        else
        {
            std::vector<const wchar_t*> paths;
            for (const auto& path : inputs) { paths.push_back(path.c_str()); }
            decode.inputs = paths.data();
            decode.inputCount = static_cast<uint32_t>(paths.size());
            decode.outputPath = output.c_str();
            decode.fileId = fileId.empty() ? nullptr : fileId.c_str();
            status = d2i_decode(&decode, &report, error.data(), error.size());
        }
        if (status == D2I_OK) { printReport(report); }
        return status;
    }
    catch (const std::invalid_argument& error) { d2i_log_message("ERROR", error.what()); return D2I_ARGUMENT; }
    catch (const std::exception& error) { d2i_log_message("ERROR", error.what()); return D2I_INTERNAL; }
}
