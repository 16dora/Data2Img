#include "cli.h"
#include "data2img.h"
#include <iostream>

// 输入：系统命令行；输出：退出码；作用：日志初始化后转交 CLI。
int wmain(int argc, wchar_t* argv[])
{
    if (d2i_init_logging(0) != D2I_OK)
    {
        std::cerr << "Cannot initialize Data2Img stderr logging\n";
        return D2I_INTERNAL;
    }
    return runCli(argc, argv);
}
