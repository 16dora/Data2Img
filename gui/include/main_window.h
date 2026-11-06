#pragma once

#include "codec_worker.h"
#include <QMainWindow>
#include <QSet>
#include <memory>

namespace Ui
{
class MainWindow;
}

// UI 线程独占页面数据，编解码通过单次 Worker 串行执行。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // 输入：父窗口；输出：主窗口；作用：建立两个页面及成员函数连接。
    explicit MainWindow(QWidget* parent = nullptr);
    // 输入：无；输出：无；作用：在任务已结束的前提下回收界面。
    ~MainWindow() override;

protected:
    // 输入：关闭事件；输出：接受或拒绝；作用：核心尚无取消接口，运行期间保留任务资源。
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 输入：无；输出：选中文件；作用：打开单文件选择框。
    void onSelectInput();
    // 输入：路径列表；输出：当前编码文件；作用：合并选择和拖放入口。
    void onEncodeFiles(const QStringList& paths);
    // 输入：无；输出：输出目录；作用：选择编码图片的保存目录。
    void onSelectOutput();
    // 输入：无；输出：后台任务；作用：校验参数后开始编码。
    void onEncodeClicked();
    // 输入：无；输出：图片列表；作用：多选 PNG/JPG/JPEG 并追加。
    void onSelectImages();
    // 输入：路径列表；输出：去重后的输入；作用：统一校验并追加选择或拖放的图片。
    void onDecodeFiles(const QStringList& paths);
    // 输入：无；输出：空列表；作用：同时清除界面、错误和内部输入集合。
    void onClearClicked();
    // 输入：无；输出：后台任务；作用：选择恢复目标后开始解码。
    void onDecodeClicked();
    // 输入：队列结果；输出：页面结果；作用：在 UI 线程显示成功路径或错误。
    void onTaskResult(const CodecResult& result);
    // 输入：线程结束信号；输出：空闲状态；作用：线程完全结束后开放下一次操作。
    void onTaskFinished();

private:
    // 输入：无；输出：控件状态；作用：集中维护 Busy、按钮和输入区域使能。
    void updateUiState();
    // 输入：任务；输出：工作线程；作用：复制任务并建立队列结果连接。
    void startTask(const CodecTask& task);
    // 输入：目标页面、错误码、说明；输出：可见错误和统一日志；作用：普通错误不弹窗。
    void showFailure(bool isEncode, int code, const QString& message);

    std::unique_ptr<Ui::MainWindow> ui; // UI 线程持有 Designer 界面。
    QString m_inputFile; // 当前编码绝对路径。
    QStringList m_decodeFiles; // 仅保存输入图片，绝不从显示文本反向解析。
    QSet<QString> m_fileKeys; // Windows 规范路径的大小写不敏感去重键。
    CodecWorker* m_workerPtr = nullptr; // QObject 父子关系持有，结束后延迟删除。
    bool m_isBusy = false; // 整个 GUI 同时只允许一项编解码任务。
};
