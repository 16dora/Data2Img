#include "main_window.h"
#include "data2img.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <Windows.h>

// 输入：原因；输出：控制台诊断；作用：仅日志初始化失败时直接输出并退出。
static void printLogFailure(const QString& reason)
{
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        AllocConsole();
        handle = GetStdHandle(STD_ERROR_HANDLE);
    }
    const QByteArray bytes = (QStringLiteral("Data2Img GUI logging initialization failed: ") + reason + QLatin1Char('\n')).toUtf8();
    DWORD written = 0;
    WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

// 输入：命令行；输出：应用退出码；作用：先为统一日志准备 stderr，再创建业务窗口和工作线程。
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Data2Img"));
    QCoreApplication::setApplicationName(QStringLiteral("Data2ImgGui"));
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    if (!QDir().mkpath(folder))
    {
        printLogFailure(QStringLiteral("Cannot create log directory: ") + folder);
        return D2I_IO;
    }
    const std::wstring path = QDir(folder).filePath(QStringLiteral("gui.log")).toStdWString();
    const HANDLE oldHandle = GetStdHandle(STD_ERROR_HANDLE);
    const HANDLE logHandle = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logHandle == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        printLogFailure(QStringLiteral("CreateFileW failed, Win32=%1 path=%2").arg(code).arg(QString::fromStdWString(path)));
        return D2I_IO;
    }
    if (!SetStdHandle(STD_ERROR_HANDLE, logHandle) || d2i_init_logging(0) != D2I_OK)
    {
        const DWORD code = GetLastError();
        SetStdHandle(STD_ERROR_HANDLE, oldHandle);
        CloseHandle(logHandle);
        printLogFailure(QStringLiteral("stderr initialization failed, Win32=%1").arg(code));
        return D2I_INTERNAL;
    }
    d2i_log_message("INFO", "GUI main() >>");
    int result = 0;
    {
        // 窗口先析构，日志借用句柄最后关闭，保证 Worker 与 QObject 清理期间日志仍然有效。
        MainWindow window;
        window.show();
        result = app.exec();
    }
    d2i_log_message("INFO", QStringLiteral("GUI main() << result=%1").arg(result).toUtf8().constData());
    SetStdHandle(STD_ERROR_HANDLE, oldHandle);
    CloseHandle(logHandle);
    return result;
}
