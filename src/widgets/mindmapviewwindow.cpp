#include "mindmapviewwindow.h"

#include <QToolBar>
#include <QSplitter>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QMetaObject>
#include <QTimer>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <core/htmltemplatehelper.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <utils/utils.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>

#include "toolbarhelper.h"
#include "findandreplacewidget.h"
#include "editors/mindmapeditor.h"
#include "editors/mindmapeditoradapter.h"
#include "viewarea.h"

using namespace vnotex;

MindMapViewWindow::MindMapViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = ViewWindowMode::Edit;
    setupUI();
}

void MindMapViewWindow::setupUI()
{
    setupEditor();
    setCentralWidget(m_editor);

    setupToolBar();
}

void MindMapViewWindow::setupEditor()
{
    Q_ASSERT(!m_editor);

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &mindMapEditorConfig = editorConfig.getMindMapEditorConfig();

    updateConfigRevision();

    HtmlTemplateHelper::updateMindMapEditorTemplate(mindMapEditorConfig);

    auto adapter = new MindMapEditorAdapter(nullptr);
    qDebug() << "MindMapViewWindow::setupEditor: Created adapter:" << adapter;
    
    m_editor = new MindMapEditor(adapter,
                                 VNoteX::getInst().getThemeMgr().getBaseBackground(),
                                 1.0,
                                 this);
    qDebug() << "MindMapViewWindow::setupEditor: Created editor:" << m_editor;
    
    connect(m_editor, &MindMapEditor::contentsChanged,
            this, [this]() {
                getBuffer()->setModified(m_editor->isModified());
                getBuffer()->invalidateContent(
                    this, [this](int p_revision) {
                        this->setBufferRevisionAfterInvalidation(p_revision);
                    });
            });

    // 连接URL点击信号
    connect(adapter, &MindMapEditorAdapter::urlClickRequested,
            this, &MindMapViewWindow::handleUrlClick);
    
    // 连接带方向的URL点击信号
    connect(adapter, &MindMapEditorAdapter::urlClickWithDirectionRequested,
            this, &MindMapViewWindow::handleUrlClickWithDirection);
}

QString MindMapViewWindow::getLatestContent() const
{
    QString content;
    adapter()->saveData([&content](const QString &p_data) {
        content = p_data;
    });

    while (content.isNull()) {
        Utils::sleepWait(50);
    }

    return content;
}

QString MindMapViewWindow::selectedText() const
{
    return m_editor->selectedText();
}

void MindMapViewWindow::setMode(ViewWindowMode p_mode)
{
    Q_UNUSED(p_mode);
    Q_ASSERT(false);
}

void MindMapViewWindow::openTwice(const QSharedPointer<FileOpenParameters> &p_paras)
{
    Q_UNUSED(p_paras);
}

ViewWindowSession MindMapViewWindow::saveSession() const
{
    auto session = ViewWindow::saveSession();
    return session;
}

void MindMapViewWindow::applySnippet(const QString &p_name)
{
    Q_UNUSED(p_name);
}

void MindMapViewWindow::applySnippet()
{
}

void MindMapViewWindow::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
{
    Q_UNUSED(p_callback);
}

void MindMapViewWindow::handleEditorConfigChange()
{
    if (updateConfigRevision()) {
        const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
        const auto &mindMapEditorConfig = editorConfig.getMindMapEditorConfig();

        HtmlTemplateHelper::updateMindMapEditorTemplate(mindMapEditorConfig);
    }
}

void MindMapViewWindow::setModified(bool p_modified)
{
    m_editor->setModified(p_modified);
}

void MindMapViewWindow::print()
{
}

void MindMapViewWindow::syncEditorFromBuffer()
{
    auto buffer = getBuffer();
    if (buffer) {
        m_editor->setHtml(HtmlTemplateHelper::getMindMapEditorTemplate(),
                          PathUtils::pathToUrl(buffer->getContentPath()));
        adapter()->setData(buffer->getContent());
        m_editor->setModified(buffer->isModified());
    } else {
        m_editor->setHtml("");
        adapter()->setData("");
        m_editor->setModified(false);
    }

    m_bufferRevision = buffer ? buffer->getRevision() : 0;
}

void MindMapViewWindow::syncEditorFromBufferContent()
{
    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    adapter()->setData(buffer->getContent());
    m_editor->setModified(buffer->isModified());
    m_bufferRevision = buffer->getRevision();
}

void MindMapViewWindow::scrollUp()
{
}

void MindMapViewWindow::scrollDown()
{
}

void MindMapViewWindow::zoom(bool p_zoomIn)
{
    Q_UNUSED(p_zoomIn);
}

MindMapEditorAdapter *MindMapViewWindow::adapter() const
{
    if (m_editor) {
        return dynamic_cast<MindMapEditorAdapter *>(m_editor->adapter());
    }

    return nullptr;
}

bool MindMapViewWindow::updateConfigRevision()
{
    bool changed = false;

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    if (m_editorConfigRevision != editorConfig.revision()) {
        changed = true;
        m_editorConfigRevision = editorConfig.revision();
    }

    if (m_editorConfigRevision != editorConfig.getMindMapEditorConfig().revision()) {
        changed = true;
        m_editorConfigRevision = editorConfig.getMindMapEditorConfig().revision();
    }

    return changed;
}

void MindMapViewWindow::setupToolBar()
{
    auto toolBar = createToolBar(this);
    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::Save);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::Attachment);

    addAction(toolBar, ViewWindowToolBarHelper::Tag);

    ToolBarHelper::addSpacer(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::FindAndReplace);

    addAction(toolBar, ViewWindowToolBarHelper::Debug);
}

void MindMapViewWindow::toggleDebug()
{
    if (m_debugViewer) {
        bool shouldEnable = !m_debugViewer->isVisible();
        m_debugViewer->setVisible(shouldEnable);
        m_editor->page()->setDevToolsPage(shouldEnable ? m_debugViewer->page() : nullptr);
    } else {
        setupDebugViewer();
        m_editor->page()->setDevToolsPage(m_debugViewer->page());
    }
}

void MindMapViewWindow::setupDebugViewer()
{
    Q_ASSERT(!m_debugViewer);

    // Need a vertical QSplitter to hold the original QSplitter and the debug viewer.
    auto mainSplitter = new QSplitter(this);
    mainSplitter->setContentsMargins(0, 0, 0, 0);
    mainSplitter->setOrientation(Qt::Vertical);

    replaceCentralWidget(mainSplitter);

    mainSplitter->addWidget(m_editor);
    mainSplitter->setFocusProxy(m_editor);

    m_debugViewer = new WebViewer(VNoteX::getInst().getThemeMgr().getBaseBackground(), this);
    m_debugViewer->resize(m_editor->width(), m_editor->height() / 2);
    mainSplitter->addWidget(m_debugViewer);
}

void MindMapViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options)
{
    if (p_options & FindOption::IncrementalSearch) {
        m_editor->findText(p_text, p_options);
    }
}

void MindMapViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options)
{
    // We do not use mark.js for searching as the contents are mainly SVG.
    m_editor->findText(p_texts.empty() ? QString() : p_texts[0], p_options);
}

void MindMapViewWindow::handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
    Q_UNUSED(p_replaceText);
    showMessage(tr("Replace is not supported yet"));
}

void MindMapViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    Q_UNUSED(p_text);
    Q_UNUSED(p_options);
    Q_UNUSED(p_replaceText);
    showMessage(tr("Replace is not supported yet"));
}

void MindMapViewWindow::handleFindAndReplaceWidgetClosed()
{
    m_editor->findText(QString(), FindOption::FindNone);
}

void MindMapViewWindow::showFindAndReplaceWidget()
{
    bool isFirstTime = !m_findAndReplace;
    ViewWindow::showFindAndReplaceWidget();
    if (isFirstTime) {
        m_findAndReplace->setReplaceEnabled(false);
        m_findAndReplace->setOptionsEnabled(FindOption::WholeWordOnly | FindOption::RegularExpression, false);
    }
}

// 思维导图 link 增强功能, 支持打开 url 里的内容, 支持多方向打开
void MindMapViewWindow::handleUrlClick(const QString &p_url)
{
    if (p_url.isEmpty()) {
        return;
    }

    qDebug() << "MindMapViewWindow: Handling URL click:" << p_url;

    // 检查是否为本地文件路径
    QString filePath = p_url;
    
    // 如果是相对路径，尝试相对于当前文件解析
    if (QFileInfo(filePath).isRelative()) {
        auto buffer = getBuffer();
        if (buffer) {
            const QString basePath = QFileInfo(buffer->getContentPath()).absolutePath();
            filePath = QDir(basePath).absoluteFilePath(p_url);
        }
    }

    // 检查文件是否存在
    if (QFileInfo::exists(filePath)) {
        // 获取当前脑图所在的ViewSplit
        auto currentSplit = getViewSplit();
        if (!currentSplit) {
            // 如果无法获取当前split，使用原来的逻辑
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            qInfo() << "Requested to open file in new workspace (fallback):" << filePath;
            return;
        }

        // 查找ViewArea
        ViewArea *viewArea = nullptr;
        QWidget *parent = currentSplit->parentWidget();
        while (parent && !viewArea) {
            viewArea = dynamic_cast<ViewArea *>(parent);
            parent = parent->parentWidget();
        }

        if (!viewArea) {
            qWarning() << "Could not find ViewArea, using fallback";
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            return;
        }

        // 查找是否已有合适的目标split（右边的split）
        ViewSplit *targetSplit = nullptr;
        const auto &allSplits = viewArea->getAllViewSplits();
        
        // 尝试找到当前脑图右边的split
        for (auto split : allSplits) {
            if (split != currentSplit) {
                // 简单策略：如果有其他split，就使用第一个找到的
                targetSplit = split;
                break;
            }
        }

        if (targetSplit) {
            // 如果找到了目标split，直接在其中打开文件
            qDebug() << "Found existing target split, opening file directly";
            
            // 设置目标split为当前split，这样文件会在那里打开
            viewArea->setCurrentViewSplit(targetSplit, true);
            
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            
            qInfo() << "Opened file in existing target split:" << filePath;
        } else {
            // 如果没有目标split，创建一个新的空split（默认右边）
            emit currentSplit->emptySplitRequested(currentSplit, Direction::Right);
            
            // 延迟打开文件
            QTimer::singleShot(50, this, [filePath]() {
                auto paras = QSharedPointer<FileOpenParameters>::create();
                paras->m_alwaysNewWindow = true;
                paras->m_focus = true;
                emit VNoteX::getInst().openFileRequested(filePath, paras);
                qInfo() << "Opened file in newly created empty split:" << filePath;
            });
            
            qInfo() << "Created new empty split and scheduled file opening:" << filePath;
        }
        
    } else if (p_url.startsWith("http://") || p_url.startsWith("https://")) {
        // 处理HTTP/HTTPS链接，使用系统默认程序打开
        WidgetUtils::openUrlByDesktop(QUrl(p_url));
        qInfo() << "Opened URL with system default program:" << p_url;
    } else {
        // 文件不存在或URL格式不支持
        showMessage(tr("File does not exist or unsupported URL format: %1").arg(p_url));
        qWarning() << "File does not exist or unsupported URL:" << p_url;
    }
}

void MindMapViewWindow::handleUrlClickWithDirection(const QString &p_url, const QString &p_direction)
{
    if (p_url.isEmpty()) {
        return;
    }

    qDebug() << "MindMapViewWindow: Handling URL click with direction:" << p_url << "Direction:" << p_direction;

    // 将字符串方向转换为Direction枚举
    Direction direction = Direction::Right; // 默认右边
    if (p_direction == "Up") {
        direction = Direction::Up;
    } else if (p_direction == "Down") {
        direction = Direction::Down;
    } else if (p_direction == "Left") {
        direction = Direction::Left;
    } else if (p_direction == "Right") {
        direction = Direction::Right;
    }

    // 检查是否为本地文件路径
    QString filePath = p_url;
    
    // 如果是相对路径，尝试相对于当前文件解析
    if (QFileInfo(filePath).isRelative()) {
        auto buffer = getBuffer();
        if (buffer) {
            const QString basePath = QFileInfo(buffer->getContentPath()).absolutePath();
            filePath = QDir(basePath).absoluteFilePath(p_url);
        }
    }

    // 检查文件是否存在
    if (QFileInfo::exists(filePath)) {
        // 获取当前脑图所在的ViewSplit
        auto currentSplit = getViewSplit();
        if (!currentSplit) {
            // 如果无法获取当前split，使用原来的逻辑
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            qInfo() << "Requested to open file in new workspace (fallback):" << filePath;
            return;
        }

        // 查找ViewArea
        ViewArea *viewArea = nullptr;
        QWidget *parent = currentSplit->parentWidget();
        while (parent && !viewArea) {
            viewArea = dynamic_cast<ViewArea *>(parent);
            parent = parent->parentWidget();
        }

        if (!viewArea) {
            qWarning() << "Could not find ViewArea, using fallback";
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            return;
        }

        // 清理无效的split引用
        cleanupInvalidSplits(viewArea);

        // 查找指定方向是否已有目标split
        ViewSplit *targetSplit = m_directionSplits.value(p_direction, nullptr);
        
        // 验证target split是否仍然有效
        if (targetSplit) {
            const auto &allSplits = viewArea->getAllViewSplits();
            if (!allSplits.contains(targetSplit)) {
                // split已经被删除，清除映射
                m_directionSplits.remove(p_direction);
                targetSplit = nullptr;
                qDebug() << "Removed invalid split for direction:" << p_direction;
            }
        }

        if (targetSplit && targetSplit != currentSplit) {
            // 如果找到了有效的目标split，直接在其中打开文件
            qDebug() << "Found existing target split for direction:" << p_direction;
            
            viewArea->setCurrentViewSplit(targetSplit, true);
            
            auto paras = QSharedPointer<FileOpenParameters>::create();
            paras->m_alwaysNewWindow = true;
            paras->m_focus = true;
            emit VNoteX::getInst().openFileRequested(filePath, paras);
            
            qInfo() << "Opened file in existing target split with direction:" << p_direction << filePath;
        } else {
            // 如果没有目标split，根据指定方向创建新的空split
            qDebug() << "Creating new empty split in direction:" << p_direction;
            emit currentSplit->emptySplitRequested(currentSplit, direction);
            
            // 延迟打开文件，并记录新创建的split
            QTimer::singleShot(100, this, [this, filePath, p_direction, viewArea]() {
                // 查找新创建的split（应该是最新的）
                const auto &allSplits = viewArea->getAllViewSplits();
                ViewSplit *newSplit = nullptr;
                
                for (auto split : allSplits) {
                    if (split != getViewSplit() && !m_directionSplits.values().contains(split)) {
                        newSplit = split;
                        break;
                    }
                }
                
                if (newSplit) {
                    // 记录这个方向对应的split
                    m_directionSplits[p_direction] = newSplit;
                    qDebug() << "Recorded new split for direction:" << p_direction;
                }
                
                auto paras = QSharedPointer<FileOpenParameters>::create();
                paras->m_alwaysNewWindow = true;
                paras->m_focus = true;
                emit VNoteX::getInst().openFileRequested(filePath, paras);
                qInfo() << "Opened file in newly created empty split with direction:" << p_direction << filePath;
            });
            
            qInfo() << "Created new empty split in direction:" << p_direction << "and scheduled file opening:" << filePath;
        }
        
    } else if (p_url.startsWith("http://") || p_url.startsWith("https://")) {
        // 处理HTTP/HTTPS链接，使用系统默认程序打开
        WidgetUtils::openUrlByDesktop(QUrl(p_url));
        qInfo() << "Opened URL with system default program:" << p_url;
    } else {
        // 文件不存在或URL格式不支持
        showMessage(tr("File does not exist or unsupported URL format: %1").arg(p_url));
        qWarning() << "File does not exist or unsupported URL:" << p_url;
    }
}

void MindMapViewWindow::cleanupInvalidSplits(ViewArea *viewArea)
{
    if (!viewArea) {
        return;
    }

    const auto &validSplits = viewArea->getAllViewSplits();
    QStringList invalidDirections;

    // 检查每个记录的split是否仍然有效
    for (auto it = m_directionSplits.begin(); it != m_directionSplits.end(); ++it) {
        if (!validSplits.contains(it.value())) {
            invalidDirections.append(it.key());
        }
    }

    // 移除无效的映射
    for (const QString &direction : invalidDirections) {
        m_directionSplits.remove(direction);
        qDebug() << "Cleaned up invalid split for direction:" << direction;
    }
}
