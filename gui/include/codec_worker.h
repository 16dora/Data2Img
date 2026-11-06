#pragma once

#include "data2img.h"
#include <QThread>
#include <QStringList>

// 任务启动前由 UI 复制，运行期间不再修改。
struct CodecTask
{
    bool isEncode = true; // true 编码，false 解码。
    QStringList inputs; // 已校验的绝对路径。
    QString output; // 编码输出目录或解码目标文件。
    quint32 widthPx = 3840; // 编码画布宽度，单位像素。
    quint32 heightPx = 2060; // 编码画布高度，单位像素。
    quint32 cellPx = 2; // 主网格色块边长，单位像素。
    bool isOverwrite = false; // 是否已允许覆盖输出。
};

// 通过队列信号按值返回 UI，不跨线程共享可变结果。
struct CodecResult
{
    bool isEncode = true; // 对应任务页面。
    int status = D2I_INTERNAL; // core 状态码或宿主异常错误码。
    QString message; // UTF-8 错误转换后的界面文本。
    QStringList files; // 成功时本次生成的绝对路径。
    D2iReport report{}; // core 返回的完整性验证结果。
};

Q_DECLARE_METATYPE(CodecResult)

// 单次后台任务；不增加核心的进度、取消或并行接口。
class CodecWorker final : public QThread
{
    Q_OBJECT

public:
    // 输入：任务快照、父对象；输出：线程对象；作用：持有同步调用所需的全部路径。
    explicit CodecWorker(const CodecTask& task, QObject* parent = nullptr);

signals:
    // 输入：独立结果副本；输出：队列信号；作用：让 UI 线程显示成功或失败。
    void resultReady(const CodecResult& result);

protected:
    // 输入：任务快照；输出：结果信号；作用：在工作线程调用 core 并封闭宿主异常。
    void run() override;

private:
    const CodecTask m_task; // 构造后不可变，后台线程读取。
};
