#include "mindmapviewwindow.h"

#include <QToolBar>
#include <QSplitter>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <core/htmltemplatehelper.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <utils/utils.h>
#include <utils/pathutils.h>

#include "toolbarhelper.h"
#include "findandreplacewidget.h"
#include "editors/mindmapeditor.h"
#include "editors/mindmapeditoradapter.h"

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
    m_editor = new MindMapEditor(adapter,
                                 VNoteX::getInst().getThemeMgr().getBaseBackground(),
                                 1.0,
                                 this);
    connect(m_editor, &MindMapEditor::contentsChanged,
            this, [this]() {
                getBuffer()->setModified(m_editor->isModified());
                getBuffer()->invalidateContent(
                    this, [this](int p_revision) {
                        this->setBufferRevisionAfterInvalidation(p_revision);
                    });
            });
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
