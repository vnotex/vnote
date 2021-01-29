#include "markdownviewwindow.h"

#include <QSplitter>
#include <QToolBar>
#include <QStackedWidget>
#include <QEvent>
#include <QCoreApplication>
#include <QScrollBar>
#include <QLabel>

#include <core/fileopenparameters.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/htmltemplatehelper.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/pegmarkdownhighlighter.h>
#include <vtextedit/markdowneditorconfig.h>
#include <utils/pathutils.h>
#include <buffer/markdownbuffer.h>
#include <core/vnotex.h>
#include <core/thememgr.h>
#include "editors/markdowneditor.h"
#include "textviewwindowhelper.h"
#include "editors/markdownviewer.h"
#include "editors/editormarkdownvieweradapter.h"
#include "editors/previewhelper.h"
#include "dialogs/deleteconfirmdialog.h"
#include "outlineprovider.h"
#include "toolbarhelper.h"
#include "findandreplacewidget.h"
#include "editors/statuswidget.h"

using namespace vnotex;

MarkdownViewWindow::MarkdownViewWindow(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
    : ViewWindow(p_parent),
      m_openParas(p_paras)
{
    // Need to setup before setupUI() since the tool bar action will need the provider.
    setupOutlineProvider();

    setupUI();

    m_previewHelper = new PreviewHelper(nullptr, this);

    setModeInternal(modeFromOpenParameters(*p_paras));

    m_initialized = true;
}

MarkdownViewWindow::~MarkdownViewWindow()
{
    if (m_textEditorStatusWidget) {
        getMainStatusWidget()->removeWidget(m_textEditorStatusWidget.get());
        m_textEditorStatusWidget->setParent(nullptr);
    }

    if (m_viewerStatusWidget) {
        getMainStatusWidget()->removeWidget(m_viewerStatusWidget.get());
        m_viewerStatusWidget->setParent(nullptr);
    }

    m_mainStatusWidget->setParent(nullptr);
}

void MarkdownViewWindow::setupUI()
{
    // Central widget.
    m_splitter = new QSplitter(this);
    m_splitter->setContentsMargins(0, 0, 0, 0);
    setCentralWidget(m_splitter);

    // Get the focus event from splitter.
    m_splitter->installEventFilter(this);

    // Status widget.
    // We use a QTabWidget as status widget since we have two widgets for different modes.
    {
        auto statusWidget = QSharedPointer<StatusWidget>::create(this);
        m_mainStatusWidget = QSharedPointer<QStackedWidget>::create(this);
        m_mainStatusWidget->setContentsMargins(0, 0, 0, 0);
        statusWidget->setEditorStatusWidget(m_mainStatusWidget);
        setStatusWidget(statusWidget);
    }

    setupToolBar();
}

void MarkdownViewWindow::setMode(Mode p_mode)
{
    setModeInternal(p_mode);

    if (m_findAndReplace && m_findAndReplace->isVisible()) {
        m_findAndReplace->setReplaceEnabled(m_mode != Mode::Read);
    }
}

void MarkdownViewWindow::setModeInternal(Mode p_mode)
{
    if (p_mode == m_mode) {
        return;
    }

    m_previousMode = m_mode;
    m_mode = p_mode;

    switch (m_mode) {
    case Mode::Read:
    {
        if (!m_viewer) {
            setupViewer();
            if (m_initialized) {
                syncViewerFromBuffer(true);
            }
        }

        // Avoid focus glitch.
        m_viewer->show();
        m_viewer->setFocus();

        if (m_editor) {
            m_editor->hide();
        }

        getMainStatusWidget()->setCurrentWidget(m_viewerStatusWidget.get());
        break;
    }

    case Mode::Edit:
    {
        if (!m_editor) {
            // We need viewer to preview.
            if (!m_viewer) {
                setupViewer();
            }

            setupTextEditor();

            if (m_initialized) {
                syncTextEditorFromBuffer(true);
            }
        }

        // Avoid focus glitch.
        m_editor->show();
        m_editor->setFocus();

        Q_ASSERT(m_viewer);
        m_viewer->hide();

        getMainStatusWidget()->setCurrentWidget(m_textEditorStatusWidget.get());
        break;
    }

    default:
        Q_ASSERT(false);
        break;
    }

    // Let editor to show or scrollToLine will not work correctly.
    QCoreApplication::processEvents();

    if (m_initialized) {
        doSyncEditorFromBufferContent(true);
    }

    emit modeChanged();
}

void MarkdownViewWindow::setModified(bool p_modified)
{
    if (m_editor) {
        m_editor->setModified(p_modified);
    }
}

void MarkdownViewWindow::handleEditorConfigChange()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    if (markdownEditorConfig.revision() != m_markdownEditorConfigRevision) {
        m_markdownEditorConfigRevision = markdownEditorConfig.revision();
        HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownEditorConfig);
        if (m_editor) {
            auto config = createMarkdownEditorConfig(markdownEditorConfig);
            m_editor->setConfig(config);

            m_editor->updateFromConfig();
        }

        updateWebViewerConfig(markdownEditorConfig);
    }
}

QString MarkdownViewWindow::getLatestContent() const
{
    Q_ASSERT(m_editor);
    return m_editor->getText();
}

void MarkdownViewWindow::syncEditorFromBuffer()
{
    m_bufferRevision = getBuffer() ? getBuffer()->getRevision() : 0;
    syncTextEditorFromBuffer(false);
    syncViewerFromBuffer(false);
}

void MarkdownViewWindow::syncEditorFromBufferContent()
{
    doSyncEditorFromBufferContent(false);
}

void MarkdownViewWindow::doSyncEditorFromBufferContent(bool p_syncPosition)
{
    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    // m_bufferRevision may already be the same as the buffer revision, in which
    // case we will call editor or viewer to update its content.
    m_bufferRevision = buffer->getRevision();
    switch (m_mode) {
    case Mode::Read:
        syncViewerFromBufferContent(p_syncPosition);
        break;

    case Mode::Edit:
        syncTextEditorFromBufferContent(p_syncPosition);
        break;

    default:
        Q_ASSERT(false);
    }
}

void MarkdownViewWindow::handleBufferChangedInternal()
{
    TextViewWindowHelper::handleBufferChanged(this);

    auto buffer = getBuffer();
    if (m_openParas && m_openParas->m_newFile) {
        Q_ASSERT(m_mode != Mode::Read);
        const auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
        if (markdownEditorConfig.getInsertFileNameAsTitle() && buffer->getContent().isEmpty()) {
            const auto title = QString("# %1\n").arg(QFileInfo(buffer->getName()).completeBaseName());
            m_editor->insertText(title);
        }
    }

    m_openParas.clear();
}

void MarkdownViewWindow::setupToolBar()
{
    auto toolBar = createToolBar(this);

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const int iconSize = editorConfig.getToolBarIconSize();
    toolBar->setIconSize(QSize(iconSize, iconSize));

    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::EditReadDiscard);
    addAction(toolBar, ViewWindowToolBarHelper::Save);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::Attachment);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::SectionNumber);

    addAction(toolBar, ViewWindowToolBarHelper::TypeHeading);
    addAction(toolBar, ViewWindowToolBarHelper::TypeBold);
    addAction(toolBar, ViewWindowToolBarHelper::TypeItalic);
    addAction(toolBar, ViewWindowToolBarHelper::TypeStrikethrough);
    addAction(toolBar, ViewWindowToolBarHelper::TypeMark);
    addAction(toolBar, ViewWindowToolBarHelper::TypeUnorderedList);
    addAction(toolBar, ViewWindowToolBarHelper::TypeOrderedList);
    addAction(toolBar, ViewWindowToolBarHelper::TypeTodoList);
    addAction(toolBar, ViewWindowToolBarHelper::TypeCheckedTodoList);
    addAction(toolBar, ViewWindowToolBarHelper::TypeCode);
    addAction(toolBar, ViewWindowToolBarHelper::TypeCodeBlock);
    addAction(toolBar, ViewWindowToolBarHelper::TypeMath);
    addAction(toolBar, ViewWindowToolBarHelper::TypeMathBlock);
    addAction(toolBar, ViewWindowToolBarHelper::TypeQuote);
    addAction(toolBar, ViewWindowToolBarHelper::TypeLink);
    addAction(toolBar, ViewWindowToolBarHelper::TypeImage);
    addAction(toolBar, ViewWindowToolBarHelper::TypeTable);

    ToolBarHelper::addSpacer(toolBar);
    addAction(toolBar, ViewWindowToolBarHelper::FindAndReplace);
    addAction(toolBar, ViewWindowToolBarHelper::Outline);
}

void MarkdownViewWindow::setupTextEditor()
{
    Q_ASSERT(!m_editor);
    Q_ASSERT(m_viewer);
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    m_editorConfigRevision = editorConfig.revision();
    m_markdownEditorConfigRevision = markdownEditorConfig.revision();

    m_editor = new MarkdownEditor(markdownEditorConfig,
                                  createMarkdownEditorConfig(markdownEditorConfig),
                                  this);
    m_splitter->insertWidget(0, m_editor);

    TextViewWindowHelper::connectEditor(this);

    // Status widget.
    m_textEditorStatusWidget = m_editor->statusWidget();
    getMainStatusWidget()->addWidget(m_textEditorStatusWidget.get());
    m_textEditorStatusWidget->show();

    m_previewHelper->setMarkdownEditor(m_editor);
    m_editor->setPreviewHelper(m_previewHelper);

    // Connect viewer and editor.
    connect(adapter(), &MarkdownViewerAdapter::viewerReady,
            m_editor->getHighlighter(), &vte::PegMarkdownHighlighter::updateHighlight);
    connect(m_editor, &MarkdownEditor::htmlToMarkdownRequested,
            adapter(), &MarkdownViewerAdapter::htmlToMarkdownRequested);
    connect(adapter(), &MarkdownViewerAdapter::htmlToMarkdownReady,
            m_editor, &MarkdownEditor::handleHtmlToMarkdownData);

    // Connect outline pipeline.
    connect(m_editor, &MarkdownEditor::headingsChanged,
            this, [this]() {
                if (getMode() != Mode::Read) {
                    auto outline = headingsToOutline(m_editor->getHeadings());
                    m_outlineProvider->setOutline(outline);
                }
            });
    connect(m_editor, &MarkdownEditor::currentHeadingChanged,
            this, [this]() {
                if (getMode() != Mode::Read) {
                    m_outlineProvider->setCurrentHeadingIndex(m_editor->getCurrentHeadingIndex());
                }
            });

    connect(m_editor, &MarkdownEditor::readRequested,
            this, [this]() {
                read(true);
            });
}

QStackedWidget *MarkdownViewWindow::getMainStatusWidget() const
{
    return m_mainStatusWidget.data();
}

bool MarkdownViewWindow::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_obj == m_splitter) {
        if (p_event->type() == QEvent::FocusIn) {
            focusEditor();
        }
    }

    return ViewWindow::eventFilter(p_obj, p_event);
}

void MarkdownViewWindow::focusEditor()
{
    switch (m_mode) {
    case Mode::Read:
        m_viewer->setFocus();
        break;

    case Mode::Edit:
        m_editor->setFocus();
        break;

    default:
        Q_ASSERT(false);
        break;
    }
}

void MarkdownViewWindow::setupViewer()
{
    Q_ASSERT(!m_viewer);
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    m_editorConfigRevision = editorConfig.revision();
    m_markdownEditorConfigRevision = markdownEditorConfig.revision();

    HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownEditorConfig);

    auto adapter = new EditorMarkdownViewerAdapter(nullptr, this);
    m_viewer = new MarkdownViewer(adapter,
                                  VNoteX::getInst().getThemeMgr().getBaseBackground(),
                                  markdownEditorConfig.getZoomFactorInReadMode(),
                                  this);
    m_splitter->addWidget(m_viewer);

    // Status widget.
    {
        // TODO: implement a real status widget for viewer.
        auto label = new QLabel(tr("Markdown Viewer"), this);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_viewerStatusWidget.reset(label);
        getMainStatusWidget()->addWidget(m_viewerStatusWidget.get());
        m_viewerStatusWidget->show();
    }

    m_viewer->setPreviewHelper(m_previewHelper);

    connect(m_viewer, &MarkdownViewer::zoomFactorChanged,
            this, [this](qreal p_factor) {
                auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
                markdownEditorConfig.setZoomFactorInReadMode(p_factor);
                showZoomFactor(p_factor);
            });

    connect(m_viewer, &WebViewer::linkHovered,
            this, [this](const QString &p_url) {
                showMessage(p_url);
            });

    connect(m_viewer, &MarkdownViewer::editRequested,
            this, [this]() {
                edit();
            });

    // Connect outline pipeline.
    connect(adapter, &MarkdownViewerAdapter::headingsChanged,
            this, [this]() {
                if (getMode() == Mode::Read) {
                    auto outline = headingsToOutline(this->adapter()->getHeadings());
                    m_outlineProvider->setOutline(outline);
                }
            });
    connect(adapter, &MarkdownViewerAdapter::currentHeadingChanged,
            this, [this]() {
                if (getMode() == Mode::Read) {
                    m_outlineProvider->setCurrentHeadingIndex(this->adapter()->getCurrentHeadingIndex());
                }
            });
    connect(adapter, &MarkdownViewerAdapter::findTextReady,
            this, [this](const QString &p_text, int p_totalMatches, int p_currentMatchIndex) {
                this->showFindResult(p_text, p_totalMatches, p_currentMatchIndex);
            });
}

void MarkdownViewWindow::syncTextEditorFromBuffer(bool p_syncPositionFromReadMode)
{
    if (!m_editor) {
        return;
    }

    const bool old = m_propogateEditorToBuffer;
    m_propogateEditorToBuffer = false;

    auto buffer = getBuffer();
    m_editor->setBuffer(buffer);
    if (buffer) {
        m_editor->setReadOnly(buffer->isReadOnly());
        m_editor->setBasePath(buffer->getResourcePath());
        m_editor->setText(buffer->getContent());
        m_editor->setModified(buffer->isModified());

        int lineNumber = -1;
        if (p_syncPositionFromReadMode) {
            lineNumber = getReadLineNumber();
        }
        m_editor->scrollToLine(lineNumber, false);
    } else {
        m_editor->setReadOnly(true);
        m_editor->setBasePath("");
        m_editor->setText("");
        m_editor->setModified(false);
    }

    m_textEditorBufferRevision = m_bufferRevision;
    m_propogateEditorToBuffer = old;
}

void MarkdownViewWindow::syncViewerFromBuffer(bool p_syncPositionFromEditMode)
{
    if (!m_viewer) {
        return;
    }

    auto buffer = getBuffer();
    adapter()->setBuffer(buffer);
    if (buffer) {
        int lineNumber = -1;
        if (p_syncPositionFromEditMode) {
            lineNumber = getEditLineNumber();
        }

        // TODO: Check buffer for last position recover.

        // Use getPath() instead of getBasePath() to make in-page anchor work.
        m_viewer->setHtml(HtmlTemplateHelper::getMarkdownViewerTemplate(),
                          PathUtils::pathToUrl(buffer->getContentPath()));
        adapter()->setText(m_bufferRevision, buffer->getContent(), lineNumber);
    } else {
        m_viewer->setHtml("");
        adapter()->setText(0, "", -1);
    }
    m_viewerBufferRevision = m_bufferRevision;
}

void MarkdownViewWindow::syncTextEditorFromBufferContent(bool p_syncPosition)
{
    Q_ASSERT(m_editor);
    if (m_textEditorBufferRevision == m_bufferRevision) {
        if (p_syncPosition) {
            m_editor->scrollToLine(getReadLineNumber(), false);
        }
        return;
    }

    const bool old = m_propogateEditorToBuffer;
    m_propogateEditorToBuffer = false;

    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    m_editor->setText(buffer->getContent());
    m_editor->setModified(buffer->isModified());

    m_textEditorBufferRevision = m_bufferRevision;
    m_propogateEditorToBuffer = old;
}

void MarkdownViewWindow::syncViewerFromBufferContent(bool p_syncPosition)
{
    Q_ASSERT(m_viewer);
    if (m_viewerBufferRevision == m_bufferRevision) {
        if (p_syncPosition) {
            adapter()->scrollToPosition(MarkdownViewerAdapter::Position(getEditLineNumber(), QString()));
        }
        return;
    }

    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    adapter()->setText(m_bufferRevision,
                       buffer->getContent(),
                       p_syncPosition ? getEditLineNumber() : -1);

    m_viewerBufferRevision = m_bufferRevision;
}

void MarkdownViewWindow::setBufferRevisionAfterInvalidation(int p_bufferRevision)
{
    m_bufferRevision = p_bufferRevision;
    switch (m_mode) {
    case Mode::Edit:
        m_textEditorBufferRevision = p_bufferRevision;
        break;

    case Mode::Read:
        m_viewerBufferRevision = p_bufferRevision;
        break;

    default:
        Q_ASSERT(false);
    }
}

EditorMarkdownViewerAdapter *MarkdownViewWindow::adapter() const
{
    if (m_viewer) {
        return dynamic_cast<EditorMarkdownViewerAdapter *>(m_viewer->adapter());
    }

    return nullptr;
}

int MarkdownViewWindow::getEditLineNumber() const
{
    if (m_previousMode == Mode::Edit || m_previousMode == Mode::FocusPreview) {
        if (m_editor) {
            return m_editor->getTopLine();
        }
    }

    return -1;
}

int MarkdownViewWindow::getReadLineNumber() const
{
    if (m_previousMode == Mode::Read) {
        if (m_viewer) {
            return adapter()->getTopLineNumber();
        }
    }

    return -1;
}

void MarkdownViewWindow::handleTypeAction(TypeAction p_action)
{
    Q_ASSERT(inModeCanInsert() && m_editor);
    Q_ASSERT(!getBuffer()->isReadOnly());
    switch (p_action) {
    case TypeAction::Heading1:
        Q_FALLTHROUGH();
    case TypeAction::Heading2:
        Q_FALLTHROUGH();
    case TypeAction::Heading3:
        Q_FALLTHROUGH();
    case TypeAction::Heading4:
        Q_FALLTHROUGH();
    case TypeAction::Heading5:
        Q_FALLTHROUGH();
    case TypeAction::Heading6:
        m_editor->typeHeading(p_action - TypeAction::Heading1 + 1);
        break;

    case TypeAction::HeadingNone:
        m_editor->typeHeading(0);
        break;

    case TypeAction::Bold:
        m_editor->typeBold();
        break;

    case TypeAction::Italic:
        m_editor->typeItalic();
        break;

    case TypeAction::Strikethrough:
        m_editor->typeStrikethrough();
        break;

    case TypeAction::Mark:
        m_editor->typeMark();
        break;

    case TypeAction::UnorderedList:
        m_editor->typeUnorderedList();
        break;

    case TypeAction::OrderedList:
        m_editor->typeOrderedList();
        break;

    case TypeAction::TodoList:
        m_editor->typeTodoList(false);
        break;

    case TypeAction::CheckedTodoList:
        m_editor->typeTodoList(true);
        break;

    case TypeAction::Code:
        m_editor->typeCode();
        break;

    case TypeAction::CodeBlock:
        m_editor->typeCodeBlock();
        break;

    case TypeAction::Math:
        m_editor->typeMath();
        break;

    case TypeAction::MathBlock:
        m_editor->typeMathBlock();
        break;

    case TypeAction::Quote:
        m_editor->typeQuote();
        break;

    case TypeAction::Link:
        m_editor->typeLink();
        break;

    case TypeAction::Image:
        m_editor->typeImage();
        break;

    case TypeAction::Table:
        m_editor->typeTable();
        break;

    default:
        qWarning() << "TypeAction not handled" << p_action;
        break;
    }
}

void MarkdownViewWindow::handleSectionNumberOverride(OverrideState p_state)
{
    if (m_mode != Mode::Read) {
        m_editor->overrideSectionNumber(p_state);
    }
}

void MarkdownViewWindow::detachFromBufferInternal()
{
    auto buffer = getBuffer();
    if (buffer && buffer->getAttachViewWindowCount() == 1) {
        const auto state = buffer->state();
        if (state == Buffer::StateFlag::Normal || state == Buffer::StateFlag::Discarded) {
            // We are the last ViewWindow of this buffer. Clear obsolete images.
            clearObsoleteImages();
        }
    }
}

void MarkdownViewWindow::clearObsoleteImages()
{
    const auto obsoleteImages = static_cast<MarkdownBuffer *>(getBuffer())->clearObsoleteImages();
    if (obsoleteImages.isEmpty()) {
        return;
    }

    auto buffer = getBuffer();
    Q_ASSERT(buffer);
    auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
    if (markdownEditorConfig.getConfirmBeforeClearObsoleteImages()) {
        QVector<ConfirmItemInfo> items;
        for (auto const &imgPath : obsoleteImages) {
            items.push_back(ConfirmItemInfo(imgPath, imgPath, imgPath, nullptr));
        }

        DeleteConfirmDialog dialog(tr("Clear Obsolete Images"),
            tr("These images seems not in use anymore. Please confirm the deletion of them."),
            tr("Deleted images could be found in the recycle bin of notebook if it is from a bundle notebook."),
            items,
            DeleteConfirmDialog::Flag::AskAgain | DeleteConfirmDialog::Flag::Preview,
            false,
            this);
        if (dialog.exec() == QDialog::Accepted) {
            items = dialog.getConfirmedItems();
            markdownEditorConfig.setConfirmBeforeClearObsoleteImages(!dialog.isNoAskChecked());
            for (const auto &item : items) {
                buffer->removeImage(item.m_path);
            }
        }
    } else {
        for (const auto &imgPath : obsoleteImages) {
            buffer->removeImage(imgPath);
        }
    }
}

QSharedPointer<OutlineProvider> MarkdownViewWindow::getOutlineProvider()
{
    return m_outlineProvider;
}

template <class T>
QSharedPointer<Outline> MarkdownViewWindow::headingsToOutline(const QVector<T> &p_headings)
{
    auto outline = QSharedPointer<Outline>::create();
    if (!p_headings.isEmpty()) {
        outline->m_headings.reserve(p_headings.size());
        for (const auto &heading : p_headings) {
            outline->m_headings.push_back(Outline::Heading(heading.m_name, heading.m_level));
        }
    }

    return outline;
}

void MarkdownViewWindow::setupOutlineProvider()
{
    m_outlineProvider.reset(new OutlineProvider(nullptr));
    connect(m_outlineProvider.data(), &OutlineProvider::headingClicked,
            this, [this](int p_idx) {
                switch (getMode()) {
                case Mode::Read:
                    adapter()->scrollToHeading(p_idx);
                    break;

                default:
                    m_editor->scrollToHeading(p_idx);
                    break;
                }
            });
}

QSharedPointer<vte::MarkdownEditorConfig> MarkdownViewWindow::createMarkdownEditorConfig(const MarkdownEditorConfig &p_config)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    auto textEditorConfig = TextViewWindowHelper::createTextEditorConfig(p_config.getTextEditorConfig(),
                                                                         themeMgr.getFile(Theme::File::MarkdownEditorStyle),
                                                                         themeMgr.getMarkdownEditorHighlightTheme());
    auto editorConfig = QSharedPointer<vte::MarkdownEditorConfig>::create(textEditorConfig);
    editorConfig->m_constrainInPlacePreviewWidthEnabled = p_config.getConstrainInPlacePreviewWidthEnabled();
    return editorConfig;
}

void MarkdownViewWindow::scrollUp()
{
    if (m_mode == Mode::Read) {
        adapter()->scroll(true);
    } else {
        QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
        if (vbar && (vbar->minimum() != vbar->maximum())) {
            vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        }
    }
}

void MarkdownViewWindow::scrollDown()
{
    if (m_mode == Mode::Read) {
        adapter()->scroll(false);
    } else {
        QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
        if (vbar && (vbar->minimum() != vbar->maximum())) {
            vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        }
    }
}

void MarkdownViewWindow::updateWebViewerConfig(const MarkdownEditorConfig &p_config)
{
    if (!m_viewer) {
        return;
    }

    m_viewer->setZoomFactor(p_config.getZoomFactorInReadMode());
}

void MarkdownViewWindow::zoom(bool p_zoomIn)
{
    // Only editor will receive the wheel event.
    Q_ASSERT(m_mode != Mode::Read);
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    auto &textEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getTextEditorConfig();
    textEditorConfig.setZoomDelta(m_editor->zoomDelta());
    showZoomDelta(m_editor->zoomDelta());
}

void MarkdownViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options)
{
    if (m_mode == Mode::Read) {
        adapter()->findText(p_text, p_options);
    } else {
        TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
    }
}

void MarkdownViewWindow::handleFindNext(const QString &p_text, FindOptions p_options)
{
    if (m_mode == Mode::Read) {
        if (p_options & FindOption::IncrementalSearch) {
            adapter()->findText(p_text, p_options);
        }
    } else {
        TextViewWindowHelper::handleFindNext(this, p_text, p_options);
    }
}

void MarkdownViewWindow::handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    if (m_mode == Mode::Read) {
        VNoteX::getInst().showStatusMessageShort(tr("Replace is not supported in read mode"));
    } else {
        TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
    }
}

void MarkdownViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    if (m_mode == Mode::Read) {
        VNoteX::getInst().showStatusMessageShort(tr("Replace is not supported in read mode"));
    } else {
        TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
    }
}

void MarkdownViewWindow::handleFindAndReplaceWidgetClosed()
{
    if (m_editor) {
        TextViewWindowHelper::handleFindAndReplaceWidgetClosed(this);
    } else {
        adapter()->findText("", FindOption::None);
    }
}

void MarkdownViewWindow::handleFindAndReplaceWidgetOpened()
{
    Q_ASSERT(m_findAndReplace);
    m_findAndReplace->setReplaceEnabled(m_mode != Mode::Read);
}
