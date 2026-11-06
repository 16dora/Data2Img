#include "main_window.h"
#include "ui_main_window.h"
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

// 输入：父窗口；输出：界面；作用：仅使用成员函数连接，避免 Designer 自动槽重复连接。
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);
    ui->pt_inputFile->setSingleFile(true);
    qRegisterMetaType<CodecResult>();
    connect(ui->btn_selectInput, &QPushButton::clicked, this, &MainWindow::onSelectInput);
    connect(ui->pt_inputFile, &DropTextEdit::filesDropped, this, &MainWindow::onEncodeFiles);
    connect(ui->btn_selectOutput, &QPushButton::clicked, this, &MainWindow::onSelectOutput);
    connect(ui->btn_encode, &QPushButton::clicked, this, &MainWindow::onEncodeClicked);
    connect(ui->btn_selectImages, &QPushButton::clicked, this, &MainWindow::onSelectImages);
    connect(ui->pt_decodeFiles, &DropTextEdit::filesDropped, this, &MainWindow::onDecodeFiles);
    connect(ui->btn_clear, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    connect(ui->btn_decode, &QPushButton::clicked, this, &MainWindow::onDecodeClicked);
    updateUiState();
    statusBar()->showMessage(tr("就绪"));
}

// 输入：无；输出：无；作用：closeEvent 已阻止运行期间销毁线程的拥有者。
MainWindow::~MainWindow() = default;

// 输入：关闭事件；输出：关闭状态；作用：不终止后台线程，也不在 UI 线程等待耗时业务。
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isBusy)
    {
        statusBar()->showMessage(tr("任务正在执行，请完成后再关闭窗口。"));
        event->ignore();
        return;
    }
    event->accept();
}

// 输入：无；输出：选中文件；作用：取消选择时保留当前输入。
void MainWindow::onSelectInput()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择需要编码的文件"), m_inputFile);
    if (!path.isEmpty())
    {
        onEncodeFiles(QStringList{path});
    }
}

// 输入：选择或拖入的路径；输出：编码输入；作用：编码仅接收一个实际文件。
void MainWindow::onEncodeFiles(const QStringList& paths)
{
    if (m_isBusy)
    {
        return;
    }
    if (paths.size() != 1 || !QFileInfo(paths.first()).isFile())
    {
        showFailure(true, D2I_ARGUMENT, tr("请选择或拖入一个有效文件，不支持目录。"));
        return;
    }
    const QFileInfo info(paths.first());
    m_inputFile = QDir::toNativeSeparators(info.canonicalFilePath());
    ui->pt_inputFile->setPlainText(m_inputFile);
    ui->pt_encodeResult->clear();
    if (ui->le_outputDir->text().trimmed().isEmpty())
    {
        const QString folder = info.completeBaseName() + QStringLiteral("_pages");
        ui->le_outputDir->setText(QDir::toNativeSeparators(info.dir().absoluteFilePath(folder)));
    }
    d2i_log_message("INFO", QStringLiteral("Encode input selected=%1").arg(m_inputFile).toUtf8().constData());
    updateUiState();
}

// 输入：无；输出：目录；作用：选择已有目录，也允许用户在路径框中填写待创建目录。
void MainWindow::onSelectOutput()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("选择图片输出目录"), ui->le_outputDir->text());
    if (!folder.isEmpty())
    {
        ui->le_outputDir->setText(QDir::toNativeSeparators(folder));
    }
}

// 输入：编码参数；输出：后台任务；作用：控件约束数值范围，布局有效性由 core 再次检查。
void MainWindow::onEncodeClicked()
{
    if (m_isBusy)
    {
        return;
    }
    const QString folder = ui->le_outputDir->text().trimmed();
    if (m_inputFile.isEmpty() || folder.isEmpty())
    {
        showFailure(true, D2I_ARGUMENT, tr("请选择输入文件并填写图片输出目录。"));
        return;
    }
    CodecTask task;
    task.inputs.append(m_inputFile);
    task.output = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    task.widthPx = static_cast<quint32>(ui->sbox_width->value());
    task.heightPx = static_cast<quint32>(ui->sbox_height->value());
    task.cellPx = static_cast<quint32>(ui->sbox_cell->value());
    task.isOverwrite = ui->check_overwrite->isChecked();
    ui->le_outputDir->setText(QDir::toNativeSeparators(task.output));
    ui->pt_encodeResult->clear();
    startTask(task);
}

// 输入：无；输出：追加图片；作用：一次选择多个图片，取消时不改变列表。
void MainWindow::onSelectImages()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("选择需要解码的图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.PNG *.JPG *.JPEG)"));
    if (!paths.isEmpty())
    {
        onDecodeFiles(paths);
    }
}

// 输入：路径；输出：去重列表和必要错误；作用：规范路径折叠大小写与符号链接后统一去重。
void MainWindow::onDecodeFiles(const QStringList& paths)
{
    if (m_isBusy)
    {
        return;
    }
    for (const QString& path : paths)
    {
        const QFileInfo info(path);
        const QString suffix = info.suffix().toLower();
        if (!info.isFile() || (suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") && suffix != QStringLiteral("jpeg")))
        {
            // 无效项不进入输入集合，混合拖放中的其他有效图片仍可继续加载。
            showFailure(false, D2I_ARGUMENT, tr("不是有效的 PNG/JPG/JPEG 文件：%1").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
            continue;
        }
        const QString file = QDir::toNativeSeparators(info.canonicalFilePath());
        const QString key = file.toCaseFolded();
        if (m_fileKeys.contains(key))
        {
            continue;
        }
        m_fileKeys.insert(key);
        m_decodeFiles.append(file);
        ui->pt_decodeFiles->appendPlainText(file);
        d2i_log_message("INFO", QStringLiteral("Decode input added=%1").arg(file).toUtf8().constData());
    }
    updateUiState();
}

// 输入：无；输出：空页面；作用：文件集合、去重键和显示内容一起重置。
void MainWindow::onClearClicked()
{
    if (m_isBusy)
    {
        return;
    }
    m_decodeFiles.clear();
    m_fileKeys.clear();
    ui->pt_decodeFiles->clear();
    statusBar()->showMessage(tr("已清空解码列表"));
    d2i_log_message("INFO", "Decode list cleared");
    updateUiState();
}

// 输入：已加载图片；输出：任务；作用：另存为明确恢复文件名，已有目标只在确认后覆盖。
void MainWindow::onDecodeClicked()
{
    if (m_isBusy || m_decodeFiles.isEmpty())
    {
        return;
    }
    const QString initial = QFileInfo(m_decodeFiles.first()).dir().absoluteFilePath(QStringLiteral("restored.bin"));
    const QString output = QFileDialog::getSaveFileName(this, tr("保存恢复的文件（请指定扩展名）"), initial,
        tr("所有文件 (*)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (output.isEmpty())
    {
        return;
    }
    const bool isExisting = QFileInfo::exists(output);
    if (isExisting)
    {
        // 覆盖恢复文件属于明确的替换操作，默认选择不覆盖。
        const auto answer = QMessageBox::question(this, tr("确认覆盖"),
            tr("目标已存在，是否覆盖？\n%1").arg(QDir::toNativeSeparators(output)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }
    CodecTask task;
    task.isEncode = false;
    task.inputs = m_decodeFiles;
    task.output = QDir::cleanPath(QFileInfo(output).absoluteFilePath());
    task.isOverwrite = isExisting;
    startTask(task);
}

// 输入：后台结果副本；输出：路径、摘要或错误；作用：输入数据不从结果文档中读取。
void MainWindow::onTaskResult(const CodecResult& result)
{
    if (result.status != D2I_OK)
    {
        QString message = result.message;
        if (result.isEncode)
        {
            message += tr("\n编码失败时输出目录可能保留部分图片，请勿视为完整结果。");
        }
        showFailure(result.isEncode, result.status, message);
        return;
    }
    const QString summary = tr("完成：%1 字节，%2 张图片，耗时 %3 ms")
        .arg(result.report.fileBytes).arg(result.report.frameCount).arg(result.report.elapsedMs);
    if (result.isEncode)
    {
        ui->pt_encodeResult->setPlainText(result.files.join(QLatin1Char('\n')));
    }
    else
    {
        ui->pt_decodeFiles->appendPlainText(tr("[Decode 成功] %1").arg(result.files.first()));
    }
    statusBar()->showMessage(summary);
}

// 输入：finished 信号；输出：空闲；作用：结果信号先入队，线程结束后才允许下一任务。
void MainWindow::onTaskFinished()
{
    // 等待仅用于同步已退出线程的清理，不等待仍在运行的编解码业务。
    m_workerPtr->wait();
    m_workerPtr->deleteLater();
    m_workerPtr = nullptr;
    m_isBusy = false;
    updateUiState();
}

// 输入：当前数据和 Busy；输出：控件使能；作用：两个页面统一串行，防止跨页重复启动。
void MainWindow::updateUiState()
{
    ui->btn_selectInput->setEnabled(!m_isBusy);
    ui->pt_inputFile->setEnabled(!m_isBusy);
    ui->group_params->setEnabled(!m_isBusy);
    ui->btn_encode->setEnabled(!m_isBusy && !m_inputFile.isEmpty());
    ui->btn_selectImages->setEnabled(!m_isBusy);
    ui->pt_decodeFiles->setEnabled(!m_isBusy);
    ui->btn_clear->setEnabled(!m_isBusy && !ui->pt_decodeFiles->toPlainText().isEmpty());
    ui->btn_decode->setEnabled(!m_isBusy && !m_decodeFiles.isEmpty());
    ui->progress_busy->setVisible(m_isBusy);
}

// 输入：不可变任务快照；输出：启动线程；作用：跨线程只通过队列传递结果，主窗口存活至完成。
void MainWindow::startTask(const CodecTask& task)
{
    m_workerPtr = new CodecWorker(task, this);
    connect(m_workerPtr, &CodecWorker::resultReady, this, &MainWindow::onTaskResult, Qt::QueuedConnection);
    connect(m_workerPtr, &QThread::finished, this, &MainWindow::onTaskFinished, Qt::QueuedConnection);
    m_isBusy = true;
    updateUiState();
    statusBar()->showMessage(task.isEncode ? tr("正在编码，请稍候…") : tr("正在解码，请稍候…"));
    m_workerPtr->start();
}

// 输入：页面、错误码、说明；输出：可见诊断；作用：保持路径列表，错误仅追加到显示文档。
void MainWindow::showFailure(bool isEncode, int code, const QString& message)
{
    const QString text = tr("[%1 错误 %2] %3")
        .arg(isEncode ? QStringLiteral("Encode") : QStringLiteral("Decode")).arg(code).arg(message);
    if (isEncode)
    {
        ui->pt_encodeResult->appendPlainText(text);
    }
    else
    {
        ui->pt_decodeFiles->appendPlainText(text);
    }
    statusBar()->showMessage(tr("操作失败，详情见当前页面。"));
    d2i_log_message("ERROR", text.toUtf8().constData());
    updateUiState();
}
