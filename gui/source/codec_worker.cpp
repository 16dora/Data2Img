#include "codec_worker.h"
#include <QDir>
#include <array>
#include <exception>
#include <string>
#include <vector>

// 输入：任务、父对象；输出：工作线程；作用：复制输入，不持有 UI 控件指针。
CodecWorker::CodecWorker(const CodecTask& task, QObject* parent)
    : QThread(parent), m_task(task)
{
}

// 输入：只读任务；输出：结果信号；作用：调用线程阻塞仅发生在后台，路径存活至 API 返回。
void CodecWorker::run()
{
    CodecResult result;
    result.isEncode = m_task.isEncode;
    result.report.structBytes = sizeof(result.report);
    std::array<char, 4096> error{};
    const QByteArray entry = QStringLiteral("CodecWorker::run() >> mode=%1 output=%2 overwrite=%3")
        .arg(m_task.isEncode ? QStringLiteral("encode") : QStringLiteral("decode"), m_task.output)
        .arg(m_task.isOverwrite).toUtf8();
    d2i_log_message("INFO", entry.constData());
    try
    {
        const std::wstring output = m_task.output.toStdWString();
        if (m_task.isEncode)
        {
            const std::wstring input = m_task.inputs.first().toStdWString();
            D2iEncodeOptions options{};
            options.structBytes = sizeof(options);
            options.inputPath = input.c_str();
            options.outputDir = output.c_str();
            options.widthPx = m_task.widthPx;
            options.heightPx = m_task.heightPx;
            options.cellPx = m_task.cellPx;
            options.overwrite = m_task.isOverwrite ? 1U : 0U;
            const QByteArray params = QStringLiteral("d2i_encode >> input=%1 width=%2 height=%3 cell=%4")
                .arg(m_task.inputs.first()).arg(options.widthPx).arg(options.heightPx)
                .arg(options.cellPx).toUtf8();
            d2i_log_message("INFO", params.constData());
            result.status = d2i_encode(&options, &result.report, error.data(), error.size());
            result.message = QString::fromUtf8(error.data());
            const QByteArray returned = QStringLiteral("d2i_encode << status=%1 frames=%2 message=%3")
                .arg(result.status).arg(result.report.frameCount).arg(result.message).toUtf8();
            d2i_log_message(result.status == D2I_OK ? "INFO" : "ERROR", returned.constData());
            if (result.status == D2I_OK)
            {
                // core 的固定命名规则从 1 开始；仅在整个任务校验成功后公布完整输出集合。
                for (quint64 index = 1; index <= result.report.frameCount; ++index)
                {
                    const QString name = QStringLiteral("frame_%1.png").arg(index, 6, 10, QLatin1Char('0'));
                    result.files.append(QDir::toNativeSeparators(QDir(m_task.output).absoluteFilePath(name)));
                }
            }
        }
        else
        {
            std::vector<std::wstring> storage;
            std::vector<const wchar_t*> paths;
            storage.reserve(static_cast<size_t>(m_task.inputs.size()));
            paths.reserve(static_cast<size_t>(m_task.inputs.size()));
            for (const QString& input : m_task.inputs)
            {
                storage.push_back(input.toStdWString());
                const QByteArray params = QStringLiteral("d2i_decode input=%1").arg(input).toUtf8();
                d2i_log_message("INFO", params.constData());
            }
            for (const std::wstring& input : storage)
            {
                paths.push_back(input.c_str());
            }
            D2iDecodeOptions options{};
            options.structBytes = sizeof(options);
            options.inputs = paths.data();
            options.inputCount = static_cast<uint32_t>(paths.size());
            options.outputPath = output.c_str();
            options.overwrite = m_task.isOverwrite ? 1U : 0U;
            d2i_log_message("INFO", "d2i_decode >>");
            result.status = d2i_decode(&options, &result.report, error.data(), error.size());
            result.message = QString::fromUtf8(error.data());
            const QByteArray returned = QStringLiteral("d2i_decode << status=%1 frames=%2 message=%3")
                .arg(result.status).arg(result.report.frameCount).arg(result.message).toUtf8();
            d2i_log_message(result.status == D2I_OK ? "INFO" : "ERROR", returned.constData());
            if (result.status == D2I_OK)
            {
                result.files.append(QDir::toNativeSeparators(m_task.output));
            }
        }
    }
    catch (const std::exception& exception)
    {
        // core 已封闭业务异常；宿主路径转换及结果整理异常也不能逃出线程入口。
        result.status = D2I_INTERNAL;
        result.message = QString::fromUtf8(exception.what());
        result.files.clear();
    }
    catch (...)
    {
        result.status = D2I_INTERNAL;
        result.message = tr("后台任务发生未知异常。");
        result.files.clear();
    }
    const QByteArray exit = QStringLiteral("CodecWorker::run() << status=%1 bytes=%2 frames=%3 elapsed_ms=%4 message=%5")
        .arg(result.status).arg(result.report.fileBytes).arg(result.report.frameCount)
        .arg(result.report.elapsedMs).arg(result.message).toUtf8();
    d2i_log_message(result.status == D2I_OK ? "INFO" : "ERROR", exit.constData());
    emit resultReady(result);
}
