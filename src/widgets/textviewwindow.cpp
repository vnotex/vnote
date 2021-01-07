#include "textviewwindow.h"

#include <QTextDocument>
#include <QDebug>
#include <QScrollBar>
#include <QToolBar>

#include <vtextedit/vtextedit.h>
#include <core/editorconfig.h>

#include "textviewwindowhelper.h"
#include "toolbarhelper.h"
#include "editors/texteditor.h"
#include <core/vnotex.h>
#include <core/thememgr.h>
#include "editors/statuswidget.h"

using namespace vnotex;

TextViewWindow::TextViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    m_mode = Mode::Edit;
    setupUI();
}

void TextViewWindow::setupUI()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    m_editorConfigRevision = editorConfig.revision();
    m_textEditorConfigRevision = textEditorConfig.revision();

    // Central widget.
    {
        auto config = createTextEditorConfig(textEditorConfig);
        m_editor = new TextEditor(config, this);
        setCentralWidget(m_editor);

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
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const int iconSize = editorConfig.getToolBarIconSize();
    toolBar->setIconSize(QSize(iconSize, iconSize));

    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::Save);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::Attachment);

    ToolBarHelper::addSpacer(toolBar);
    addAction(toolBar, ViewWindowToolBarHelper::FindAndReplace);
}

void TextViewWindow::handleBufferChangedInternal()
{
    TextViewWindowHelper::handleBufferChanged(this);
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
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    if (m_textEditorConfigRevision != textEditorConfig.revision()) {
        m_textEditorConfigRevision = textEditorConfig.revision();
        auto config = createTextEditorConfig(textEditorConfig);
        m_editor->setConfig(config);

        updateEditorFromConfig();
    }
}

void TextViewWindow::setBufferRevisionAfterInvalidation(int p_bufferRevision)
{
    m_bufferRevision = p_bufferRevision;
}

void TextViewWindow::setMode(Mode p_mode)
{
    Q_UNUSED(p_mode);
    Q_ASSERT(false);
}

QSharedPointer<vte::TextEditorConfig> TextViewWindow::createTextEditorConfig(const TextEditorConfig &p_config)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    auto config = TextViewWindowHelper::createTextEditorConfig(p_config,
                                                               themeMgr.getFile(Theme::File::TextEditorStyle),
                                                               themeMgr.getEditorHighlightTheme());
    return config;
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

void TextViewWindow::handleFindNext(const QString &p_text, FindOptions p_options)
{
    TextViewWindowHelper::handleFindNext(this, p_text, p_options);
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
    TextViewWindowHelper::handleFindAndReplaceWidgetClosed(this);
}

void TextViewWindow::updateEditorFromConfig()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();
    if (textEditorConfig.getZoomDelta() != 0) {
        m_editor->zoom(textEditorConfig.getZoomDelta());
    }
}
