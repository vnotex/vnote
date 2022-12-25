#include "markdownviewwindow.h"

#include <QSplitter>
#include <QToolBar>
#include <QStackedWidget>
#include <QEvent>
#include <QCoreApplication>
#include <QScrollBar>
#include <QLabel>
#include <QApplication>
#include <QProgressDialog>
#include <QMenu>
#include <QActionGroup>
#include <QTimer>
#include <QPrinter>

#include <core/fileopenparameters.h>
#include <core/editorconfig.h>
#include <core/coreconfig.h>
#include <core/htmltemplatehelper.h>
#include <core/exception.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/pegmarkdownhighlighter.h>
#include <vtextedit/markdowneditorconfig.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>
#include <utils/printutils.h>
#include <utils/fileutils.h>
#include <buffer/markdownbuffer.h>
#include <core/vnotex.h>
#include <core/thememgr.h>
#include <imagehost/imagehostmgr.h>
#include <imagehost/imagehost.h>
#include "editors/markdowneditor.h"
#include "textviewwindowhelper.h"
#include "editors/markdownviewer.h"
#include "editors/markdownvieweradapter.h"
#include "editors/previewhelper.h"
#include "dialogs/deleteconfirmdialog.h"
#include "outlineprovider.h"
#include "toolbarhelper.h"
#include "findandreplacewidget.h"
#include "editors/statuswidget.h"
#include "editors/plantumlhelper.h"
#include "editors/graphvizhelper.h"
#include "messageboxhelper.h"

using namespace vnotex;

MarkdownViewWindow::MarkdownViewWindow(QWidget *p_parent)
    : ViewWindow(p_parent)
{
    // Need to setup before setupUI() since the tool bar action will need the provider.
    setupOutlineProvider();

    setupUI();

    setupPreviewHelper();
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

void MarkdownViewWindow::setMode(ViewWindowMode p_mode)
{
    setModeInternal(p_mode, true);
}

void MarkdownViewWindow::setModeInternal(ViewWindowMode p_mode, bool p_syncBuffer)
{
    if (p_mode == m_mode) {
        return;
    }

    m_previousMode = m_mode;
    m_mode = p_mode;

    switch (m_mode) {
    case ViewWindowMode::Read:
    {
        if (!m_viewer) {
            setupViewer();
            if (p_syncBuffer) {
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

    case ViewWindowMode::Edit:
    {
        if (!m_editor) {
            // We need viewer to preview.
            if (!m_viewer) {
                setupViewer();

                // Must show the viewer to let it init with the correct DPI.
                // Will hide it when ready().
                m_viewer->show();
            }

            setupTextEditor();

            if (p_syncBuffer) {
                syncTextEditorFromBuffer(true);
            }

            m_editor->show();

            setEditViewMode(ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getEditViewMode());
        } else {
            setEditViewMode(m_editViewMode);
        }

        // Avoid focus glitch.
        m_editor->show();
        m_editor->setFocus();

        getMainStatusWidget()->setCurrentWidget(m_textEditorStatusWidget.get());
        break;
    }

    default:
        Q_ASSERT(false);
        break;
    }

    // Let editor to show, or scrollToLine will not work correctly.
    QCoreApplication::processEvents();

    if (p_syncBuffer) {
        doSyncEditorFromBufferContent(true);
    }

    emit modeChanged();

    if (m_findAndReplace && m_findAndReplace->isVisible()) {
        m_findAndReplace->setReplaceEnabled(!isReadMode());
    }
}

void MarkdownViewWindow::setModified(bool p_modified)
{
    if (m_editor) {
        m_editor->setModified(p_modified);
    }
}

void MarkdownViewWindow::handleEditorConfigChange()
{
    if (updateConfigRevision()) {
        const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
        const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

        updatePreviewHelperFromConfig(markdownEditorConfig);

        HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownEditorConfig);

        if (m_editor) {
            auto config = createMarkdownEditorConfig(editorConfig, markdownEditorConfig);
            m_editor->setConfig(config);

            m_editor->updateFromConfig();

            updateEditorFromConfig();
        }

        updateWebViewerConfig();
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
    case ViewWindowMode::Read:
        syncViewerFromBufferContent(p_syncPosition);
        break;

    case ViewWindowMode::Edit:
        syncTextEditorFromBufferContent(p_syncPosition);
        break;

    default:
        Q_ASSERT(false);
    }
}

void MarkdownViewWindow::handleBufferChangedInternal(const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (getBuffer()) {
        // Will sync buffer right behind this.
        setModeInternal(p_paras ? p_paras->m_mode : ViewWindowMode::Read, false);
    }

    TextViewWindowHelper::handleBufferChanged(this);

    handleFileOpenParameters(p_paras, false);
}

void MarkdownViewWindow::setupToolBar()
{
    auto toolBar = createToolBar(this);
    addToolBar(toolBar);

    addAction(toolBar, ViewWindowToolBarHelper::EditReadDiscard);
    addAction(toolBar, ViewWindowToolBarHelper::Save);
    addAction(toolBar, ViewWindowToolBarHelper::ViewMode);
    addAction(toolBar, ViewWindowToolBarHelper::WordCount);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::Attachment);

    addAction(toolBar, ViewWindowToolBarHelper::Tag);

    toolBar->addSeparator();

    addAction(toolBar, ViewWindowToolBarHelper::SectionNumber);

    {
        auto act = addAction(toolBar, ViewWindowToolBarHelper::InplacePreview);
        connect(act, &QAction::triggered,
                this, [this](bool p_checked) {
                    if (!isReadMode()) {
                        m_editor->setInplacePreviewEnabled(p_checked);
                    }
                });
        connect(this, &ViewWindow::modeChanged,
                this, [this, act]() {
                    act->setEnabled(inModeCanInsert() && getBuffer());
                });
    }

    addAction(toolBar, ViewWindowToolBarHelper::ImageHost);

    toolBar->addSeparator();

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

    addAction(toolBar, ViewWindowToolBarHelper::Outline);
    addAction(toolBar, ViewWindowToolBarHelper::FindAndReplace);
    addAction(toolBar, ViewWindowToolBarHelper::Print);

    addAction(toolBar, ViewWindowToolBarHelper::Debug);
}

void MarkdownViewWindow::setupTextEditor()
{
    Q_ASSERT(!m_editor);
    Q_ASSERT(m_viewer);
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    updateConfigRevision();

    m_editor = new MarkdownEditor(markdownEditorConfig,
                                  createMarkdownEditorConfig(editorConfig, markdownEditorConfig),
                                  createMarkdownEditorParameters(editorConfig, markdownEditorConfig),
                                  this);
    // Always editor comes first.
    m_splitter->insertWidget(0, m_editor);

    TextViewWindowHelper::connectEditor(this);

    // Status widget.
    m_textEditorStatusWidget = m_editor->statusWidget();
    getMainStatusWidget()->addWidget(m_textEditorStatusWidget.get());
    m_textEditorStatusWidget->show();

    m_previewHelper->setMarkdownEditor(m_editor);
    m_editor->setPreviewHelper(m_previewHelper);

    m_editor->setImageHost(m_imageHost);

    updateEditorFromConfig();

    // Connect viewer and editor.
    connect(adapter(), &MarkdownViewerAdapter::ready,
            m_editor->getHighlighter(), &vte::PegMarkdownHighlighter::updateHighlight);
    connect(m_editor, &MarkdownEditor::htmlToMarkdownRequested,
            adapter(), &MarkdownViewerAdapter::htmlToMarkdownRequested);
    connect(adapter(), &MarkdownViewerAdapter::htmlToMarkdownReady,
            m_editor, &MarkdownEditor::handleHtmlToMarkdownData);
    connect(m_editor, &vte::VMarkdownEditor::externalCodeBlockHighlightRequested,
            this, &MarkdownViewWindow::handleExternalCodeBlockHighlightRequest);
    connect(adapter(), &MarkdownViewerAdapter::highlightCodeBlockReady,
            m_editor, &vte::VMarkdownEditor::handleExternalCodeBlockHighlightData);

    // Connect outline pipeline.
    connect(m_editor, &MarkdownEditor::headingsChanged,
            this, [this]() {
                if (!isReadMode()) {
                    auto outline = headingsToOutline(m_editor->getHeadings());
                    m_outlineProvider->setOutline(outline);
                }
            });
    connect(m_editor, &MarkdownEditor::currentHeadingChanged,
            this, [this]() {
                if (!isReadMode()) {
                    m_outlineProvider->setCurrentHeadingIndex(m_editor->getCurrentHeadingIndex());
                }
            });

    connect(m_editor, &MarkdownEditor::readRequested,
            this, [this]() {
                read(true);
            });

    connect(m_editor, &MarkdownEditor::applySnippetRequested,
            this, QOverload<>::of(&MarkdownViewWindow::applySnippet));
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
    case ViewWindowMode::Read:
        m_viewer->setFocus();
        break;

    case ViewWindowMode::Edit:
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

    updateConfigRevision();

    HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownEditorConfig);

    auto adapter = new MarkdownViewerAdapter(this);
    m_viewer = new MarkdownViewer(adapter,
                                  this,
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
                if (isReadMode()) {
                    auto outline = headingsToOutline(this->adapter()->getHeadings());
                    m_outlineProvider->setOutline(outline);
                }
            });
    connect(adapter, &MarkdownViewerAdapter::currentHeadingChanged,
            this, [this]() {
                if (isReadMode()) {
                    m_outlineProvider->setCurrentHeadingIndex(this->adapter()->getCurrentHeadingIndex());
                }
            });
    connect(adapter, &MarkdownViewerAdapter::findTextReady,
            this, [this](const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex) {
                this->showFindResult(p_texts, p_totalMatches, p_currentMatchIndex);
            });
    connect(adapter, &MarkdownViewerAdapter::ready,
            this, [this]() {
                m_viewerReady = true;

                if (m_mode == ViewWindowMode::Edit) {
                    setEditViewMode(m_editViewMode);
                }
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
    if (buffer) {
        int lineNumber = -1;
        if (p_syncPositionFromEditMode) {
            lineNumber = getEditLineNumber();
        }

        // TODO: Check buffer for last position recover.

        // Use getPath() instead of getBasePath() to make in-page anchor work.
        adapter()->reset();
        m_viewer->setHtml(HtmlTemplateHelper::getMarkdownViewerTemplate(),
                          PathUtils::pathToUrl(buffer->getContentPath()));
        adapter()->setText(m_bufferRevision, buffer->getContent(), lineNumber);
    } else {
        adapter()->reset();
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
    case ViewWindowMode::Edit:
        m_textEditorBufferRevision = p_bufferRevision;
        break;

    case ViewWindowMode::Read:
        m_viewerBufferRevision = p_bufferRevision;
        break;

    default:
        Q_ASSERT(false);
    }
}

MarkdownViewerAdapter *MarkdownViewWindow::adapter() const
{
    if (m_viewer) {
        return m_viewer->adapter();
    }

    return nullptr;
}

int MarkdownViewWindow::getEditLineNumber() const
{
    if (m_previousMode == ViewWindowMode::Edit) {
        if (m_editor) {
            return m_editor->getTopLine();
        }
    }

    return -1;
}

int MarkdownViewWindow::getReadLineNumber() const
{
    if (m_previousMode == ViewWindowMode::Read) {
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
    if (!isReadMode()) {
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
    auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();
    const bool clearRemote = editorConfig.isClearObsoleteImageAtImageHostEnabled();
    const auto &hostMgr = ImageHostMgr::getInst();

    QVector<QPair<QString, bool>> imagesToDelete;
    imagesToDelete.reserve(obsoleteImages.size());

    if (markdownEditorConfig.getConfirmBeforeClearObsoleteImages()) {
        QVector<ConfirmItemInfo> items;
        for (auto it = obsoleteImages.constBegin(); it != obsoleteImages.constEnd(); ++it) {
            if (!it.value() || (clearRemote && hostMgr.findByImageUrl(it.key()))) {
                const auto imgPath = it.key();
                // Use the @m_data field to denote whether it is remote.
                items.push_back(ConfirmItemInfo(imgPath, imgPath, imgPath, it.value() ? reinterpret_cast<void *>(1ULL) : nullptr));
            }
        }

        if (items.isEmpty()) {
            return;
        }

        DeleteConfirmDialog dialog(tr("Clear Obsolete Images"),
            tr("These images seems to be not in use anymore. Please confirm the deletion of them."),
            tr("Deleted local images could be found in the recycle bin of notebook if it is from a bundle notebook."),
            items,
            DeleteConfirmDialog::Flag::AskAgain | DeleteConfirmDialog::Flag::Preview,
            false,
            this);
        if (dialog.exec() == QDialog::Accepted) {
            items = dialog.getConfirmedItems();
            markdownEditorConfig.setConfirmBeforeClearObsoleteImages(!dialog.isNoAskChecked());
            for (const auto &item : items) {
                imagesToDelete.push_back(qMakePair(item.m_path, item.m_data != nullptr));
            }
        }
    } else {
        for (auto it = obsoleteImages.constBegin(); it != obsoleteImages.constEnd(); ++it) {
            if (clearRemote || !it.value()) {
                imagesToDelete.push_back(qMakePair(it.key(), it.value()));
            }
        }
    }

    if (imagesToDelete.isEmpty()) {
        return;
    }

    QProgressDialog proDlg(tr("Clearing obsolete images..."),
                           tr("Abort"),
                           0,
                           imagesToDelete.size(),
                           this);
    proDlg.setWindowModality(Qt::WindowModal);
    proDlg.setWindowTitle(tr("Clear Obsolete Images"));

    int cnt = 0;
    for (int i = 0; i < imagesToDelete.size(); ++i) {
        proDlg.setValue(i + 1);
        if (proDlg.wasCanceled()) {
            break;
        }

        proDlg.setLabelText(tr("Clear image (%1)").arg(imagesToDelete[i].first));
        if (imagesToDelete[i].second) {
            removeFromImageHost(imagesToDelete[i].first);
        } else {
            buffer->removeImage(imagesToDelete[i].first);
        }
        ++cnt;
    }

    proDlg.setValue(imagesToDelete.size());

    // It may be deleted so showMessage() is not available.
    VNoteX::getInst().showStatusMessageShort(tr("Cleared %n obsolete images", "", cnt));
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

    const auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
    if (markdownConfig.getSectionNumberMode() == MarkdownEditorConfig::SectionNumberMode::Edit) {
        outline->m_sectionNumberBaseLevel = -1;
    } else {
        outline->m_sectionNumberBaseLevel = markdownConfig.getSectionNumberBaseLevel();
        outline->m_sectionNumberEndingDot = markdownConfig.getSectionNumberStyle() == MarkdownEditorConfig::SectionNumberStyle::DigDotDigDot;
    }

    return outline;
}

void MarkdownViewWindow::setupOutlineProvider()
{
    m_outlineProvider.reset(new OutlineProvider(nullptr));
    connect(m_outlineProvider.data(), &OutlineProvider::headingClicked,
            this, [this](int p_idx) {
                switch (getMode()) {
                case ViewWindowMode::Read:
                    adapter()->scrollToHeading(p_idx);
                    break;

                default:
                    m_editor->scrollToHeading(p_idx);
                    break;
                }
            });
}

QSharedPointer<vte::MarkdownEditorConfig> MarkdownViewWindow::createMarkdownEditorConfig(const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_config)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    auto textEditorConfig = TextViewWindowHelper::createTextEditorConfig(p_config.getTextEditorConfig(),
                                                                         p_editorConfig.getViConfig(),
                                                                         themeMgr.getFile(Theme::File::MarkdownEditorStyle),
                                                                         themeMgr.getMarkdownEditorHighlightTheme(),
                                                                         p_editorConfig.getLineEndingPolicy());

    auto editorConfig = QSharedPointer<vte::MarkdownEditorConfig>::create(textEditorConfig);
    editorConfig->overrideTextFontFamily(p_config.getEditorOverriddenFontFamily());

    editorConfig->m_constrainInplacePreviewWidthEnabled = p_config.getConstrainInplacePreviewWidthEnabled();

    {
        auto srcs = p_config.getInplacePreviewSources();
        vte::MarkdownEditorConfig::InplacePreviewSources editorSrcs = vte::MarkdownEditorConfig::NoInplacePreview;
        if (srcs & MarkdownEditorConfig::InplacePreviewSource::ImageLink) {
            editorSrcs |= vte::MarkdownEditorConfig::ImageLink;
        }
        if (srcs & MarkdownEditorConfig::InplacePreviewSource::CodeBlock) {
            editorSrcs |= vte::MarkdownEditorConfig::CodeBlock;
        }
        if (srcs & MarkdownEditorConfig::InplacePreviewSource::Math) {
            editorSrcs |= vte::MarkdownEditorConfig::Math;
        }
        editorConfig->m_inplacePreviewSources = editorSrcs;
    }

    return editorConfig;
}

QSharedPointer<vte::TextEditorParameters> MarkdownViewWindow::createMarkdownEditorParameters(const EditorConfig &p_editorConfig, const MarkdownEditorConfig &p_config)
{
    auto paras = QSharedPointer<vte::TextEditorParameters>::create();
    paras->m_spellCheckEnabled = p_config.isSpellCheckEnabled();
    paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
    paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
    return paras;
}

void MarkdownViewWindow::scrollUp()
{
    if (isReadMode()) {
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
    if (isReadMode()) {
        adapter()->scroll(false);
    } else {
        QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
        if (vbar && (vbar->minimum() != vbar->maximum())) {
            vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        }
    }
}

void MarkdownViewWindow::updateWebViewerConfig()
{
    if (!m_viewer) {
        return;
    }

    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
    const auto &markdownEditorConfig = editorConfig.getMarkdownEditorConfig();

    m_viewer->setZoomFactor(markdownEditorConfig.getZoomFactorInReadMode());
}

void MarkdownViewWindow::zoom(bool p_zoomIn)
{
    // Only editor will receive the wheel event.
    Q_ASSERT(!isReadMode());
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    auto &textEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().getTextEditorConfig();
    textEditorConfig.setZoomDelta(m_editor->zoomDelta());
    showZoomDelta(m_editor->zoomDelta());
}

void MarkdownViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options)
{
    if (isReadMode()) {
        if (p_options & FindOption::IncrementalSearch) {
            adapter()->findText(QStringList(p_text), p_options);
        }
    } else {
        TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
    }
}

void MarkdownViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options)
{
    if (isReadMode()) {
        adapter()->findText(p_texts, p_options);
    } else {
        TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
    }
}

void MarkdownViewWindow::handleReplace(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    if (isReadMode()) {
        showMessage(tr("Replace is not supported in read mode"));
    } else {
        TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
    }
}

void MarkdownViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options, const QString &p_replaceText)
{
    if (isReadMode()) {
        showMessage(tr("Replace is not supported in read mode"));
    } else {
        TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
    }
}

void MarkdownViewWindow::handleFindAndReplaceWidgetClosed()
{
    if (isReadMode()) {
        adapter()->findText(QStringList(), FindOption::FindNone);
    } else {
        TextViewWindowHelper::clearSearchHighlights(this);
    }
}

void MarkdownViewWindow::clearHighlights()
{
    if (isReadMode()) {
        adapter()->findText(QStringList(), FindOption::FindNone);
    } else {
        TextViewWindowHelper::clearSearchHighlights(this);
    }
}

void MarkdownViewWindow::handleFindAndReplaceWidgetOpened()
{
    Q_ASSERT(m_findAndReplace);
    m_findAndReplace->setReplaceEnabled(!isReadMode());
}

void MarkdownViewWindow::handleFileOpenParameters(const QSharedPointer<FileOpenParameters> &p_paras, bool p_twice)
{
    if (!p_paras) {
        return;
    }

    auto buffer = getBuffer();
    if (p_paras->m_newFile) {
        Q_ASSERT(!isReadMode());
        const auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
        if (markdownEditorConfig.getInsertFileNameAsTitle() && buffer->getContent().isEmpty()) {
            const auto title = QString("# %1\n").arg(QFileInfo(buffer->getName()).completeBaseName());
            m_editor->insertText(title);
        }
    } else {
        if (!p_twice || p_paras->m_forceMode) {
            setMode(p_paras->m_mode);
        }

        scrollToLine(p_paras->m_lineNumber);

        if (p_paras->m_searchToken) {
            findTextBySearchToken(p_paras->m_searchToken, p_paras->m_lineNumber);
        }
    }
}

void MarkdownViewWindow::scrollToLine(int p_lineNumber)
{
    if (p_lineNumber < 0) {
        return;
    }

    if (isReadMode()) {
        Q_ASSERT(m_viewer);
        adapter()->scrollToPosition(MarkdownViewerAdapter::Position(p_lineNumber, QString()));
    } else {
        Q_ASSERT(m_editor);
        m_editor->scrollToLine(p_lineNumber, true);
    }
}

void MarkdownViewWindow::findTextBySearchToken(const QSharedPointer<SearchToken> &p_token, int p_currentMatchLine)
{
    if (isReadMode()) {
        Q_ASSERT(m_viewer);
        const auto patterns = p_token->toPatterns();
        updateLastFindInfo(patterns.first, patterns.second);
        adapter()->findText(patterns.first, patterns.second, p_currentMatchLine);
    } else {
        Q_ASSERT(m_editor);
        TextViewWindowHelper::findTextBySearchToken(this, p_token, p_currentMatchLine);
    }
}

bool MarkdownViewWindow::isReadMode() const
{
    return m_mode == ViewWindowMode::Read;
}

void MarkdownViewWindow::openTwice(const QSharedPointer<FileOpenParameters> &p_paras)
{
    Q_ASSERT(!p_paras || !p_paras->m_newFile);
    handleFileOpenParameters(p_paras, true);
}

ViewWindowSession MarkdownViewWindow::saveSession() const
{
    auto session = ViewWindow::saveSession();
    if (getBuffer()) {
        session.m_lineNumber = isReadMode() ? adapter()->getTopLineNumber()
                                            : m_editor->getCursorPosition().first;
    }
    return session;
}

void MarkdownViewWindow::setupPreviewHelper()
{
    Q_ASSERT(!m_previewHelper);

    m_previewHelper = new PreviewHelper(nullptr, this);

    const auto &markdownEditorConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
    updatePreviewHelperFromConfig(markdownEditorConfig);
}

void MarkdownViewWindow::updatePreviewHelperFromConfig(const MarkdownEditorConfig &p_config)
{
    m_previewHelper->setWebPlantUmlEnabled(p_config.getWebPlantUml());
    m_previewHelper->setWebGraphvizEnabled(p_config.getWebGraphviz());

    const auto srcs = p_config.getInplacePreviewSources();
    m_previewHelper->setInplacePreviewCodeBlocksEnabled(srcs & MarkdownEditorConfig::CodeBlock);
    m_previewHelper->setInplacePreviewMathBlocksEnabled(srcs & MarkdownEditorConfig::Math);
}

void MarkdownViewWindow::applySnippet(const QString &p_name)
{
    if (isReadMode()) {
        qWarning() << "failed to apply snippet in read mode" << p_name;
        return;
    }

    TextViewWindowHelper::applySnippet(this, p_name);
}

void MarkdownViewWindow::applySnippet()
{
    if (isReadMode()) {
        qWarning() << "failed to apply snippet in read mode";
        return;
    }

    TextViewWindowHelper::applySnippet(this);
}

QPoint MarkdownViewWindow::getFloatingWidgetPosition()
{
    return TextViewWindowHelper::getFloatingWidgetPosition(this);
}

QString MarkdownViewWindow::selectedText() const
{
    switch (m_mode) {
    case ViewWindowMode::Read:
        Q_ASSERT(m_viewer);
        return m_viewer->selectedText();

    case ViewWindowMode::Edit:
        Q_ASSERT(m_editor);
        return m_editor->getTextEdit()->selectedText();

    default:
        return QString();
    }
}

void MarkdownViewWindow::handleImageHostChanged(const QString &p_hostName)
{
    m_imageHost = ImageHostMgr::getInst().find(p_hostName);

    if (m_editor) {
        m_editor->setImageHost(m_imageHost);
    }
}

void MarkdownViewWindow::removeFromImageHost(const QString &p_url)
{
    auto host = ImageHostMgr::getInst().findByImageUrl(p_url);
    if (!host) {
        return;
    }

    QString errMsg;
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    auto ret = host->remove(p_url, errMsg);
    QApplication::restoreOverrideCursor();

    if (!ret) {
        MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                 QString("Failed to delete image (%1) from image host (%2).").arg(p_url, host->getName()),
                                 QString(),
                                 errMsg,
                                 this);
    }
}

bool MarkdownViewWindow::updateConfigRevision()
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

    if (m_markdownEditorConfigRevision != editorConfig.getMarkdownEditorConfig().revision()) {
        changed = true;
        m_markdownEditorConfigRevision = editorConfig.getMarkdownEditorConfig().revision();
    }

    return changed;
}

void MarkdownViewWindow::toggleDebug()
{
    Q_ASSERT(m_viewer);
    if (m_debugViewer) {
        bool shouldEnable = !m_debugViewer->isVisible();
        m_debugViewer->setVisible(shouldEnable);
        m_viewer->page()->setDevToolsPage(shouldEnable ? m_debugViewer->page() : nullptr);
    } else {
        setupDebugViewer();
        m_viewer->page()->setDevToolsPage(m_debugViewer->page());
    }
}

void MarkdownViewWindow::setupDebugViewer()
{
    Q_ASSERT(!m_debugViewer);

    // Need a vertical QSplitter to hold the original QSplitter and the debug viewer.
    auto mainSplitter = new QSplitter(this);
    mainSplitter->setContentsMargins(0, 0, 0, 0);
    mainSplitter->setOrientation(Qt::Vertical);

    replaceCentralWidget(mainSplitter);

    mainSplitter->addWidget(m_splitter);
    mainSplitter->setFocusProxy(m_splitter);

    m_debugViewer = new WebViewer(VNoteX::getInst().getThemeMgr().getBaseBackground(), this);
    m_debugViewer->resize(m_splitter->width(), m_splitter->height() / 2);
    mainSplitter->addWidget(m_debugViewer);
}

void MarkdownViewWindow::updateViewModeMenu(QMenu *p_menu)
{
    p_menu->clear();

    if (isReadMode()) {
        auto act = p_menu->addAction(tr("View Mode Not Supported In Read Mode"));
        act->setEnabled(false);
        return;
    }

    if (!m_viewModeActionGroup) {
        m_viewModeActionGroup = new QActionGroup(this);
        connect(m_viewModeActionGroup, &QActionGroup::triggered,
                this, [this](QAction *act) {
                    auto mode = static_cast<MarkdownEditorConfig::EditViewMode>(act->data().toInt());
                    if (mode != m_editViewMode) {
                        ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig().setEditViewMode(mode);
                        setEditViewMode(mode);
                    }
                });
    }

    {
        auto act = p_menu->addAction(tr("Edit Only"));
        act->setCheckable(true);
        act->setData(static_cast<int>(MarkdownEditorConfig::EditViewMode::EditOnly));
        m_viewModeActionGroup->addAction(act);

        if (act->data().toInt() == m_editViewMode) {
            act->setChecked(true);
        }
    }

    {
        auto act = p_menu->addAction(tr("Edit with Preview"));
        act->setCheckable(true);
        act->setData(static_cast<int>(MarkdownEditorConfig::EditViewMode::EditPreview));
        m_viewModeActionGroup->addAction(act);

        if (act->data().toInt() == m_editViewMode) {
            act->setChecked(true);
        }
    }
}

void MarkdownViewWindow::setEditViewMode(MarkdownEditorConfig::EditViewMode p_mode)
{
    Q_ASSERT(m_mode == ViewWindowMode::Edit);

    bool modeChanged = false;
    if (m_editViewMode != p_mode) {
        m_editViewMode = p_mode;
        modeChanged = true;
    }

    switch (p_mode) {
    case MarkdownEditorConfig::EditViewMode::EditOnly:
    {
        if (m_viewerReady) {
            m_viewer->hide();
        }

        if (modeChanged) {
            disconnect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged,
                       m_syncPreviewTimer, QOverload<>::of(&QTimer::start));
            disconnect(m_editor, &MarkdownEditor::topLineChanged,
                       this, &MarkdownViewWindow::syncEditorPositionToPreview);
        }
        break;
    }

    case MarkdownEditorConfig::EditViewMode::EditPreview:
    {
        m_viewer->show();
        WidgetUtils::distributeWidgetsOfSplitter(m_splitter);

        if (modeChanged) {
            if (!m_syncPreviewTimer) {
                m_syncPreviewTimer = new QTimer(this);
                m_syncPreviewTimer->setSingleShot(true);
                m_syncPreviewTimer->setInterval(300);
                connect(m_syncPreviewTimer, &QTimer::timeout,
                        this, &MarkdownViewWindow::syncEditorContentsToPreview);
            }

            connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged,
                    m_syncPreviewTimer, QOverload<>::of(&QTimer::start),
                    Qt::UniqueConnection);
            connect(m_editor, &MarkdownEditor::topLineChanged,
                    this, &MarkdownViewWindow::syncEditorPositionToPreview,
                    Qt::UniqueConnection);
        }

        syncEditorContentsToPreview();

        break;
    }

    default:
        Q_ASSERT(false);
        break;
    }
}

void MarkdownViewWindow::syncEditorContentsToPreview()
{
    if (!m_viewerReady || isReadMode() || m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
        return;
    }

    adapter()->setText(m_editor->getText(), m_editor->getTopLine());
}

void MarkdownViewWindow::syncEditorPositionToPreview()
{
    if (!m_viewerReady || isReadMode() || m_editViewMode == MarkdownEditorConfig::EditViewMode::EditOnly) {
        return;
    }

    adapter()->scrollToPosition(MarkdownViewerAdapter::Position(m_editor->getTopLine(), QString()));
}

void MarkdownViewWindow::print()
{
    if (!m_viewer || !m_viewerReady) {
        return;
    }

    auto printer = PrintUtils::promptForPrint(m_viewer->hasSelection(), this);
    if (printer) {
        m_viewer->page()->print(printer.data(), [printer](bool p_succeeded) mutable {
                    Q_UNUSED(p_succeeded);
                    printer.reset();
                });
    }
}

void MarkdownViewWindow::handleExternalCodeBlockHighlightRequest(int p_idx, quint64 p_timeStamp, const QString &p_text)
{
    static bool stylesInitialized = false;
    if (!stylesInitialized) {
        stylesInitialized = true;
        const auto file = VNoteX::getInst().getThemeMgr().getFile(Theme::File::HighlightStyleSheet);
        if (file.isEmpty()) {
            qWarning() << "no highlight style sheet specified for external code block highlight";
        } else {
            QString content;
            try {
                content = FileUtils::readTextFile(file);
            } catch (Exception &e) {
                qWarning() << "failed to read highlight style sheet for external code block highlight" << file << e.what();
            }
            adapter()->fetchStylesFromStyleSheet(content, [this](const QVector<MarkdownViewerAdapter::CssRuleStyle> *rules) {
                MarkdownEditor::ExternalCodeBlockHighlightStyles styles;

                const QString prefix(".token.");
                for (const auto &rule : *rules) {
                    bool isFirst = true;
                    QTextCharFormat fmt;

                    // Just fetch `.token.attr` styles.
                    auto selects = rule.m_selector.split(QLatin1Char(','));
                    for (const auto &sel : selects) {
                        const auto ts = sel.trimmed();
                        if (!ts.startsWith(prefix)) {
                            continue;
                        }
                        auto classList = ts.mid(prefix.size()).split(QLatin1Char('.'));
                        for (const auto &cla : classList) {
                            if (isFirst) {
                                fmt = rule.toTextCharFormat();
                                isFirst = false;
                            }
                            styles.insert(cla, fmt);
                        }
                    }
                }

                MarkdownEditor::setExternalCodeBlockHighlihgtStyles(styles);
            });
        }
    }

    adapter()->highlightCodeBlock(p_idx, p_timeStamp, p_text);
}

void MarkdownViewWindow::updateEditorFromConfig()
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    {
        vte::Key leaderKey(coreConfig.getShortcutLeaderKey());
        m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
    }
}

void MarkdownViewWindow::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
{
    auto text = selectedText();
    if (!text.isEmpty()) {
        auto info = TextViewWindowHelper::calculateWordCountInfo(text);
        info.m_isSelection = true;
        p_callback(info);
        return;
    }

    switch (m_mode) {
    case ViewWindowMode::Read:
    {
        Q_ASSERT(m_viewer);
        m_viewer->saveContent([p_callback](const QString &content) {
            auto info = TextViewWindowHelper::calculateWordCountInfo(content);
            info.m_isSelection = false;
            p_callback(info);
        });
        break;
    }

    case ViewWindowMode::Edit:
    {
        Q_ASSERT(m_editor);
        auto info = TextViewWindowHelper::calculateWordCountInfo(m_editor->getText());
        info.m_isSelection = false;
        p_callback(info);
        break;
    }

    default:
        p_callback(WordCountInfo());
        break;
    }
}
