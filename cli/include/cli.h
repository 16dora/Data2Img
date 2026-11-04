#pragma once
// 输入：Unicode 命令行；输出：退出码；作用：解析 CLI 并调用 core DLL。
int runCli(int argc, wchar_t* argv[]);
