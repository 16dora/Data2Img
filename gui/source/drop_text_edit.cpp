#include "drop_text_edit.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

// 输入：父控件；输出：控件；作用：保留文本选择复制，禁止自由编辑。
DropTextEdit::DropTextEdit(QWidget* parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setAcceptDrops(true);
}

// 输入：单文件标志；输出：无；作用：设置页面对应的文件数量限制。
void DropTextEdit::setSingleFile(bool isSingle)
{
    m_isSingle = isSingle;
}

// 输入：拖入事件；输出：接受或拒绝；作用：只允许本地文件列表进入区域。
void DropTextEdit::dragEnterEvent(QDragEnterEvent* event)
{
    if (canAcceptFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

// 输入：拖动事件；输出：接受或拒绝；作用：只读文档中不启用文字移动。
void DropTextEdit::dragMoveEvent(QDragMoveEvent* event)
{
    if (canAcceptFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

// 输入：放下事件；输出：本地文件信号；作用：页面统一处理文件选择与拖放。
void DropTextEdit::dropEvent(QDropEvent* event)
{
    if (!canAcceptFiles(event->mimeData()))
    {
        event->ignore();
        return;
    }
    QStringList paths;
    const auto urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        paths.append(url.toLocalFile());
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit filesDropped(paths);
}

// 输入：拖放数据；输出：可接收标志；作用：不允许远程 URL 或编码多文件被静默截断。
bool DropTextEdit::canAcceptFiles(const QMimeData* mime) const
{
    if (!mime->hasUrls())
    {
        return false;
    }
    const auto urls = mime->urls();
    if (urls.isEmpty() || (m_isSingle && urls.size() != 1))
    {
        return false;
    }
    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
        {
            return false;
        }
    }
    return true;
}
