#include "textviewwindow.h"

#include <QTextDocument>
#include <QDebug>
#include <QScrollBar>
#include <QToolBar>
#include <QPrinter>

#include <vtextedit/vtextedit.h>
#include <core/editorconfig.h>
#include <core/coreconfig.h>

#include "textviewwindowhelper.h"
#include "toolbarhelper.h"
#include "editors/texteditor.h"
#include <core/vnotex.h>
#include <core/thememgr.h>
#include "editors/statuswidget.h"
#include <core/fileopenparameters.h>
#include <utils/printutils.h>

using namespace vnotex;

TextViewWindow::TextViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = ViewWindowMode::Edit;
    setupUI();
}

void TextViewWindow::setupUI()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    updateConfigRevision();

    // Central widget.
    {
        m_editor = new TextEditor(createTextEditorConfig(editorConfig, textEditorConfig),
                                  createTextEditorParameters(editorConfig, textEditorConfig),
                                  this);
        setCentralWidget(m_editor);

        connect(m_editor, &TextEditor::applySnippetRequested,
                this, QOverload<>::of(&TextViewWindow::applySnippet));

        updateEditorFromConfig();
    }

    TextViewWindowHelper::connectEditor(this);

    // Status widget.
    {
        auto statusWidget = QSharedPointer<StatusWidget>::create();
        statusWidget->setEditorStatusWidget(m_editor->statusWidget());
        setStatusWidget(statusWidget);
    }

    setupToolBar();
}

void TextViewWindow::setupToolBar()
{
    auto toolBar = createToolBar(this);
    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::Save);
    addAction(toolBar, ViewWindowToolBarHelper::WordCount);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::Attachment);
    addAction(toolBar, ViewWindowToolBarHelper::Tag);

    ToolBarHelper::addSpacer(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::FindAndReplace);
    addAction(toolBar, ViewWindowToolBarHelper::Print);
}

void TextViewWindow::handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras)
{
    Q_UNUSED(p_paras);
    TextViewWindowHelper::handleBufferChanged(this);

    handleFileOpenParameters(p_paras);
}

void TextViewWindow::syncEditorFromBuffer()
{
    const bool old = m_propogateEditorToBuffer;
    m_propogateEditorToBuffer = false;

    auto buffer = getBuffer();
    if (buffer) {
        m_editor->setSyntax(QFileInfo(buffer->getPath()).suffix());
        m_editor->setReadOnly(buffer->isReadOnly());
        m_editor->setText(buffer->getContent());
        m_editor->setModified(buffer->isModified());
    } else {
        m_editor->setSyntax("");
        m_editor->setReadOnly(true);
        m_editor->setText("");
        m_editor->setModified(false);
    }

    m_bufferRevision = buffer ? buffer->getRevision() : 0;
    m_propogateEditorToBuffer = old;
}

void TextViewWindow::syncEditorFromBufferContent()
{
    const bool old = m_propogateEditorToBuffer;
    m_propogateEditorToBuffer = false;

    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    m_editor->setText(buffer->getContent());
    m_editor->setModified(buffer->isModified());

    m_bufferRevision = buffer->getRevision();
    m_propogateEditorToBuffer = old;
}

QString TextViewWindow::getLatestContent() const
{
    return m_editor->getText();
}

void TextViewWindow::setModified(bool p_modified)
{
    m_editor->setModified(p_modified);
}

void TextViewWindow::handleEditorConfigChange()
{
    if (updateConfigRevision()) {
        const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
        const auto &textEditorConfig = editorConfig.getTextEditorConfig();

        auto config = createTextEditorConfig(editorConfig, textEditorConfig);
        m_editor->setConfig(config);

        updateEditorFromConfig();
    }
}

bool TextViewWindow::updateConfigRevision()
{
    bool changed = false;

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    if (m_editorConfigRevision != editorConfig.revision()) {
        changed = true;
        m_editorConfigRevision = editorConfig.revision();
    }

    if (m_textEditorConfigRevision != editorConfig.getTextEditorConfig().revision()) {
        changed = true;
        m_textEditorConfigRevision = editorConfig.getTextEditorConfig().revision();
    }

    return changed;
}

void TextViewWindow::setBufferRevisionAfterInvalidation(int p_bufferRevision)
{
    m_bufferRevision = p_bufferRevision;
}

void TextViewWindow::setMode(ViewWindowMode p_mode)
{
    Q_UNUSED(p_mode);
    Q_ASSERT(false);
}

QSharedPointer<vte::TextEditorConfig> TextViewWindow::createTextEditorConfig(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    auto config = TextViewWindowHelper::createTextEditorConfig(p_config,
                                                               p_editorConfig.getViConfig(),
                                                               themeMgr.getFile(Theme::File::TextEditorStyle),
                                                               themeMgr.getEditorHighlightTheme(),
                                                               p_editorConfig.getLineEndingPolicy());
    return config;
}

QSharedPointer<vte::TextEditorParameters> TextViewWindow::createTextEditorParameters(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config)
{
    auto paras = QSharedPointer<vte::TextEditorParameters>::create();
    paras->m_spellCheckEnabled = p_config.isSpellCheckEnabled();
    paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
    paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
    return paras;
}

void TextViewWindow::scrollUp()
{
    QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
        vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    }
}

void TextViewWindow::scrollDown()
{
    QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
        vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
    }
}

void TextViewWindow::zoom(bool p_zoomIn)
{
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    auto &textEditorConfig = ConfigMgr::getInst().getEditorConfig().getTextEditorConfig();
    textEditorConfig.setZoomDelta(m_editor->zoomDelta());
    showZoomDelta(m_editor->zoomDelta());
}

void TextViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options)
{
    TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
}

void TextViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options)
{
    TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
}

void TextViewWindow::handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
}

void TextViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
}

void TextViewWindow::handleFindAndReplaceWidgetClosed()
{
    TextViewWindowHelper::clearSearchHighlights(this);
}

void TextViewWindow::updateEditorFromConfig()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    if (textEditorConfig.getZoomDelta() != 0) {
        m_editor->zoom(textEditorConfig.getZoomDelta());
    }

    {
        vte::Key leaderKey(coreConfig.getShortcutLeaderKey());
        m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
    }
}

void TextViewWindow::openTwice(const QSharedPointer<FileOpenParameters> &p_paras)
{
    handleFileOpenParameters(p_paras);
}

void TextViewWindow::handleFileOpenParameters(const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (!p_paras) {
        return;
    }

    if (p_paras->m_lineNumber > -1) {
        m_editor->scrollToLine(p_paras->m_lineNumber, true);
    }

    if (p_paras->m_searchToken) {
        TextViewWindowHelper::findTextBySearchToken(this, p_paras->m_searchToken, p_paras->m_lineNumber);
    }
}

ViewWindowSession TextViewWindow::saveSession() const
{
    auto session = ViewWindow::saveSession();
    if (getBuffer()) {
        session.m_lineNumber = m_editor->getCursorPosition().first;
    }
    return session;
}

void TextViewWindow::applySnippet(const QString &p_name)
{
    TextViewWindowHelper::applySnippet(this, p_name);
}

void TextViewWindow::applySnippet()
{
    TextViewWindowHelper::applySnippet(this);
}

QPoint TextViewWindow::getFloatingWidgetPosition()
{
    return TextViewWindowHelper::getFloatingWidgetPosition(this);
}

QString TextViewWindow::selectedText() const
{
    Q_ASSERT(m_editor);
    return m_editor->getTextEdit()->selectedText();
}

void TextViewWindow::print()
{
    auto printer = PrintUtils::promptForPrint(m_editor->getTextEdit()->hasSelection(), this);
    if (printer) {
        m_editor->getTextEdit()->print(printer.data());
    }
}

void TextViewWindow::clearHighlights()
{
    TextViewWindowHelper::clearSearchHighlights(this);
}

void TextViewWindow::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
{
    auto text = selectedText();
    if (text.isEmpty()) {
        text = getLatestContent();
        auto info = TextViewWindowHelper::calculateWordCountInfo(text);
        info.m_isSelection = false;
        p_callback(info);
    } else {
        auto info = TextViewWindowHelper::calculateWordCountInfo(text);
        info.m_isSelection = true;
        p_callback(info);
    }
}
