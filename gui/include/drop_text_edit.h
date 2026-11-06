#pragma once

#include <QPlainTextEdit>
#include <QStringList>

class QMimeData;

// 只接收本地文件拖放，文本内容由页面维护。
class DropTextEdit final : public QPlainTextEdit
{
    Q_OBJECT

public:
    // 输入：父控件；输出：控件；作用：建立只读的文件拖放区域。
    explicit DropTextEdit(QWidget* parent = nullptr);
    // 输入：是否只允许单文件；输出：无；作用：区分编码和解码拖放行为。
    void setSingleFile(bool isSingle);

signals:
    // 输入：本地文件路径；输出：信号；作用：交给页面校验、去重并更新显示。
    void filesDropped(const QStringList& paths);

protected:
    // 输入：拖入事件；输出：接受或拒绝；作用：拒绝纯文本、远程地址和不符合数量的拖放。
    void dragEnterEvent(QDragEnterEvent* event) override;
    // 输入：拖动事件；输出：接受或拒绝；作用：保持只读文本区域可接收文件。
    void dragMoveEvent(QDragMoveEvent* event) override;
    // 输入：放下事件；输出：文件信号；作用：不把系统拖放文本直接插入文档。
    void dropEvent(QDropEvent* event) override;

private:
    // 输入：拖放数据；输出：是否可接收；作用：统一拖入和放下阶段的边界检查。
    bool canAcceptFiles(const QMimeData* mime) const;

    bool m_isSingle = false; // 仅 UI 线程访问，编码页面只接收单个文件。
};
