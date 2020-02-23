#include <QtWidgets>
#include <QWebChannel>
#include <QFileInfo>
#include <QCoreApplication>
#include <QWebEngineProfile>
#include "vmdtab.h"
#include "vdocument.h"
#include "vnote.h"
#include "utils/vutils.h"
#include "vpreviewpage.h"
#include "pegmarkdownhighlighter.h"
#include "vconfigmanager.h"
#include "vmarkdownconverter.h"
#include "vnotebook.h"
#include "vtableofcontent.h"
#include "dialog/vfindreplacedialog.h"
#include "veditarea.h"
#include "vconstants.h"
#include "vwebview.h"
#include "vmdeditor.h"
#include "vmainwindow.h"
#include "vsnippet.h"
#include "vinsertselector.h"
#include "vsnippetlist.h"
#include "vlivepreviewhelper.h"
#include "vmathjaxinplacepreviewhelper.h"
#include "vdirectory.h"
#include "vdirectorytree.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;


VMdTab::VMdTab(VFile *p_file, VEditArea *p_editArea,
               OpenFileMode p_mode, QWidget *p_parent)
    : VEditTab(p_file, p_editArea, p_parent),
      m_editor(NULL),
      m_webViewer(NULL),
      m_document(NULL),
      m_mdConType(g_config->getMdConverterType()),
      m_enableHeadingSequence(false),
      m_backupFileChecked(false),
      m_mode(Mode::InvalidMode),
      m_livePreviewHelper(NULL),
      m_mathjaxPreviewHelper(NULL)
{
    V_ASSERT(m_file->getDocType() == DocType::Markdown);

    if (!m_file->open()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to open note <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle).arg(m_file->getName()),
                            tr("Please check if file %1 exists.").arg(m_file->fetchPath()),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    }

    HeadingSequenceType headingSequenceType = g_config->getHeadingSequenceType();
    if (headingSequenceType == HeadingSequenceType::Enabled) {
        m_enableHeadingSequence = true;
    } else if (headingSequenceType == HeadingSequenceType::EnabledNoteOnly
               && m_file->getType() == FileType::Note) {
        m_enableHeadingSequence = true;
    }

    setupUI();

    m_backupTimer = new QTimer(this);
    m_backupTimer->setSingleShot(true);
    m_backupTimer->setInterval(g_config->getFileTimerInterval());
    connect(m_backupTimer, &QTimer::timeout,
            this, [this]() {
                writeBackupFile();
            });

    m_livePreviewTimer = new QTimer(this);
    m_livePreviewTimer->setSingleShot(true);
    m_livePreviewTimer->setInterval(500);
    connect(m_livePreviewTimer, &QTimer::timeout,
            this, [this]() {
                QString text = m_webViewer->selectedText().trimmed();
                if (text.isEmpty()) {
                    return;
                }

                const LivePreviewInfo &info = m_livePreviewHelper->getLivePreviewInfo();
                if (info.isValid()) {
                    m_editor->findTextInRange(text,
                                              FindOption::CaseSensitive,
                                              true,
                                              info.m_startPos,
                                              info.m_endPos);
                }
            });

    QTimer::singleShot(50, this, [this, p_mode]() {
                if (p_mode == OpenFileMode::Edit) {
                    showFileEditMode();
                } else {
                    showFileReadMode();
                }
            });
}

void VMdTab::setupUI()
{
    m_splitter = new QSplitter(this);
    m_splitter->setOrientation(Qt::Horizontal);

    setupMarkdownViewer();

    // Setup editor when we really need it.
    m_editor = NULL;

    // The following is the image hosting initialization
    vGithubImageHosting = new VGithubImageHosting(m_file, this);
    vGiteeImageHosting = new VGiteeImageHosting(m_file, this);
    vWechatImageHosting = new VWechatImageHosting(m_file, this);
    vTencentImageHosting = new VTencentImageHosting(m_file, this);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_splitter);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

void VMdTab::showFileReadMode()
{
    m_isEditMode = false;

    // Will recover the header when web side is ready.
    m_headerFromEditMode = m_currentHeader;

    updateWebView();

    setCurrentMode(Mode::Read);

    clearSearchedWordHighlight();

    updateStatus();
}

void VMdTab::updateWebView()
{
    if (m_mdConType == MarkdownConverterType::Hoedown) {
        viewWebByConverter();
    } else {
        m_document->updateText();
        updateOutlineFromHtml(m_document->getToc());
    }
}

bool VMdTab::scrollWebViewToHeader(const VHeaderPointer &p_header)
{
    if (!m_outline.isMatched(p_header)
        || m_outline.getType() != VTableOfContentType::Anchor) {
        return false;
    }

    if (p_header.isValid()) {
        const VTableOfContentItem *item = m_outline.getItem(p_header);
        if (item) {
            if (item->m_anchor.isEmpty()) {
                return false;
            }

            m_currentHeader = p_header;
            m_document->scrollToAnchor(item->m_anchor);
        } else {
            return false;
        }
    } else {
        if (m_outline.isEmpty()) {
            // Let it be.
            m_currentHeader = p_header;
        } else {
            // Scroll to top.
            m_currentHeader = p_header;
            m_document->scrollToAnchor("");
        }
    }

    emit currentHeaderChanged(m_currentHeader);
    return true;
}

bool VMdTab::scrollEditorToHeader(const VHeaderPointer &p_header, bool p_force)
{
    if (!m_outline.isMatched(p_header)
        || m_outline.getType() != VTableOfContentType::BlockNumber) {
        return false;
    }

    VMdEditor *mdEdit = getEditor();

    int blockNumber = -1;
    if (p_header.isValid()) {
        const VTableOfContentItem *item = m_outline.getItem(p_header);
        if (item) {
            blockNumber = item->m_blockNumber;
            if (blockNumber == -1) {
                // Empty item.
                return false;
            }
        } else {
            return false;
        }
    } else {
        if (m_outline.isEmpty()) {
            // No outline and scroll to -1 index.
            // Just let it be.
            m_currentHeader = p_header;
            return true;
        } else {
            // Has outline and scroll to -1 index.
            // Scroll to top.
            blockNumber = 0;
        }
    }

    // If the cursor are now under the right title, we should not change it right at
    // the title.
    if (!p_force) {
        int curBlockNumber = mdEdit->textCursor().block().blockNumber();
        if (m_outline.indexOfItemByBlockNumber(curBlockNumber)
            == m_outline.indexOfItemByBlockNumber(blockNumber)) {
            m_currentHeader = p_header;
            return true;
        }
    }

    if (mdEdit->scrollToHeader(blockNumber)) {
        m_currentHeader = p_header;
        return true;
    } else {
        return false;
    }
}

bool VMdTab::scrollToHeaderInternal(const VHeaderPointer &p_header)
{
    if (m_isEditMode) {
        return scrollEditorToHeader(p_header);
    } else {
        return scrollWebViewToHeader(p_header);
    }
}

void VMdTab::viewWebByConverter()
{
    VMarkdownConverter mdConverter;
    QString toc;
    QString html = mdConverter.generateHtml(m_file->getContent(),
                                            g_config->getMarkdownExtensions(),
                                            toc);
    m_document->setHtml(html);
    updateOutlineFromHtml(toc);
}

void VMdTab::showFileEditMode()
{
    VHeaderPointer header(m_currentHeader);

    m_isEditMode = true;

    VMdEditor *mdEdit = getEditor();

    setCurrentMode(Mode::Edit);

    mdEdit->beginEdit();

    // If editor is not init, we need to wait for it to init headers.
    // Generally, beginEdit() will generate the headers. Wait is needed when
    // highlight completion is going to re-generate the headers.
    int nrRetry = 10;
    do {
        // We still need to wait even when there is no header to scroll since
        // setCurrentMode()'s sendPostedEvents().
        VUtils::sleepWait(100);
    } while (header.m_index > -1
             && nrRetry-- >= 0
             && (m_outline.isEmpty() || m_outline.getType() != VTableOfContentType::BlockNumber));

    scrollEditorToHeader(header, false);

    mdEdit->setFocus();
}

bool VMdTab::closeFile(bool p_forced)
{
    if (p_forced && m_isEditMode) {
        // Discard buffer content
        Q_ASSERT(m_editor);
        m_editor->reloadFile();
        m_editor->endEdit();

        showFileReadMode();
    } else {
        readFile();
    }

    return !m_isEditMode;
}

void VMdTab::editFile()
{
    if (m_isEditMode) {
        return;
    }

    showFileEditMode();
}

void VMdTab::readFile(bool p_discard)
{
    if (!m_isEditMode) {
        return;
    }

    if (m_editor && isModified()) {
        // Prompt to save the changes.
        bool modifiable = m_file->isModifiable();
        int ret = VUtils::showMessage(QMessageBox::Information, tr("Information"),
                                      tr("Note <span style=\"%1\">%2</span> has been modified.")
                                        .arg(g_config->c_dataTextStyle).arg(m_file->getName()),
                                      tr("Do you want to save your changes?"),
                                      modifiable ? (QMessageBox::Save
                                                    | QMessageBox::Discard
                                                    | QMessageBox::Cancel)
                                                 : (QMessageBox::Discard
                                                    | QMessageBox::Cancel),
                                      modifiable ? (p_discard ? QMessageBox::Discard: QMessageBox::Save)
                                                 : QMessageBox::Cancel,
                                      this);
        switch (ret) {
        case QMessageBox::Save:
            if (!saveFile()) {
                return;
            }

            V_FALLTHROUGH;

        case QMessageBox::Discard:
            m_editor->reloadFile();
            break;

        case QMessageBox::Cancel:
            // Nothing to do if user cancel this action
            return;

        default:
            qWarning() << "wrong return value from QMessageBox:" << ret;
            return;
        }
    }

    if (m_editor) {
        m_editor->endEdit();
    }

    showFileReadMode();
}

bool VMdTab::saveFile()
{
    if (!m_isEditMode) {
        return true;
    }

    Q_ASSERT(m_editor);

    if (!isModified()) {
        return true;
    }

    QString filePath = m_file->fetchPath();

    if (!m_file->isModifiable()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Could not modify a read-only note <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle).arg(filePath),
                            tr("Please save your changes to other notes manually."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return false;
    }

    bool ret = true;
    // Make sure the file already exists. Temporary deal with cases when user delete or move
    // a file.
    if (!QFileInfo::exists(filePath)) {
        qWarning() << filePath << "being written has been removed";
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"), tr("Fail to save note."),
                            tr("File <span style=\"%1\">%2</span> being written has been removed.")
                              .arg(g_config->c_dataTextStyle).arg(filePath),
                            QMessageBox::Ok, QMessageBox::Ok, this);
        ret = false;
    } else {
        m_checkFileChange = false;
        m_editor->saveFile();
        ret = m_file->save();
        if (!ret) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to save note."),
                                tr("Fail to write to disk when saving a note. Please try it again."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
            m_editor->setModified(true);
        } else {
            m_fileDiverged = false;
            m_checkFileChange = true;
        }
    }

    updateStatus();

    return ret;
}

bool VMdTab::isModified() const
{
    return (m_editor ? m_editor->isModified() : false) || m_fileDiverged;
}

void VMdTab::saveAndRead()
{
    saveFile();
    readFile();
}

void VMdTab::discardAndRead()
{
    readFile();
}

void VMdTab::setupMarkdownViewer()
{
    m_webViewer = new VWebView(m_file, this);
    connect(m_webViewer, &VWebView::editNote,
            this, &VMdTab::editFile);
    connect(m_webViewer, &VWebView::requestSavePage,
            this, &VMdTab::handleSavePageRequested);
    connect(m_webViewer, &VWebView::selectionChanged,
            this, &VMdTab::handleWebSelectionChanged);
    connect(m_webViewer, &VWebView::requestExpandRestorePreviewArea,
            this, &VMdTab::expandRestorePreviewArea);

    VPreviewPage *page = new VPreviewPage(m_webViewer);
    m_webViewer->setPage(page);
    m_webViewer->setZoomFactor(g_config->getWebZoomFactor());
    connect(page->profile(), &QWebEngineProfile::downloadRequested,
            this, &VMdTab::handleDownloadRequested);
    connect(page, &QWebEnginePage::linkHovered,
            this, &VMdTab::statusMessage);

    // Avoid white flash before loading content.
    // Setting Qt::transparent will force GrayScale antialias rendering.
    page->setBackgroundColor(g_config->getBaseBackground());

    m_document = new VDocument(m_file, m_webViewer);
    m_documentID = m_document->registerIdentifier();

    QWebChannel *channel = new QWebChannel(m_webViewer);
    channel->registerObject(QStringLiteral("content"), m_document);
    connect(m_document, &VDocument::tocChanged,
            this, &VMdTab::updateOutlineFromHtml);
    connect(m_document, SIGNAL(headerChanged(const QString &)),
            this, SLOT(updateCurrentHeader(const QString &)));
    connect(m_document, &VDocument::keyPressed,
            this, &VMdTab::handleWebKeyPressed);
    connect(m_document, &VDocument::logicsFinished,
            this, [this]() {
                if (m_ready & TabReady::ReadMode) {
                    // Recover header from edit mode.
                    scrollWebViewToHeader(m_headerFromEditMode);
                    m_headerFromEditMode.clear();
                    m_document->muteWebView(false);
                    return;
                }

                m_ready |= TabReady::ReadMode;

                tabIsReady(TabReady::ReadMode);
            });
    connect(m_document, &VDocument::textToHtmlFinished,
            this, [this](int p_identitifer, int p_id, int p_timeStamp, const QString &p_html) {
                Q_ASSERT(m_editor);
                if (m_documentID != p_identitifer) {
                    return;
                }

                m_editor->textToHtmlFinished(p_id, p_timeStamp, m_webViewer->url(), p_html);
            });
    connect(m_document, &VDocument::htmlToTextFinished,
            this, [this](int p_identitifer, int p_id, int p_timeStamp, const QString &p_text) {
                Q_ASSERT(m_editor);
                if (m_documentID != p_identitifer) {
                    return;
                }

                m_editor->htmlToTextFinished(p_id, p_timeStamp, p_text);
            });
    connect(m_document, &VDocument::wordCountInfoUpdated,
            this, [this]() {
                VEditTabInfo info = fetchTabInfo(VEditTabInfo::InfoType::All);
                if (m_isEditMode) {
                    info.m_wordCountInfo = m_document->getWordCountInfo();
                }

                emit statusUpdated(info);
            });

    page->setWebChannel(channel);

    m_webViewer->setHtml(VUtils::generateHtmlTemplate(m_mdConType),
                         m_file->getBaseUrl());

    m_splitter->addWidget(m_webViewer);
}

void VMdTab::setupMarkdownEditor()
{
    Q_ASSERT(!m_editor);

    m_editor = new VMdEditor(m_file,
                             m_document,
                             m_mdConType,
                             m_editArea->getCompleter(),
                             this);
    m_editor->setProperty("MainEditor", true);
    m_editor->setEditTab(this);
    int delta = g_config->getEditorZoomDelta();
    if (delta > 0) {
        m_editor->zoomInW(delta);
    } else if (delta < 0) {
        m_editor->zoomOutW(-delta);
    }

    connect(m_editor, &VMdEditor::headersChanged,
            this, &VMdTab::updateOutlineFromHeaders);
    connect(m_editor, SIGNAL(currentHeaderChanged(int)),
            this, SLOT(updateCurrentHeader(int)));
    connect(m_editor, &VMdEditor::statusChanged,
            this, &VMdTab::updateStatus);
    connect(m_editor, &VMdEditor::textChanged,
            this, &VMdTab::updateStatus);
    connect(g_mainWin, &VMainWindow::editorConfigUpdated,
            m_editor, &VMdEditor::updateConfig);
    connect(m_editor->object(), &VEditorObject::cursorPositionChanged,
            this, &VMdTab::updateCursorStatus);
    connect(m_editor->object(), &VEditorObject::saveAndRead,
            this, &VMdTab::saveAndRead);
    connect(m_editor->object(), &VEditorObject::discardAndRead,
            this, &VMdTab::discardAndRead);
    connect(m_editor->object(), &VEditorObject::saveNote,
            this, &VMdTab::saveFile);
    connect(m_editor->object(), &VEditorObject::statusMessage,
            this, &VEditTab::statusMessage);
    connect(m_editor->object(), &VEditorObject::vimStatusUpdated,
            this, &VEditTab::vimStatusUpdated);
    connect(m_editor->object(), &VEditorObject::requestCloseFindReplaceDialog,
            this, [this]() {
                this->m_editArea->getFindReplaceDialog()->closeDialog();
            });
    connect(m_editor->object(), &VEditorObject::ready,
            this, [this]() {
                if (m_ready & TabReady::EditMode) {
                    return;
                }

                m_ready |= TabReady::EditMode;

                tabIsReady(TabReady::EditMode);
            });
    connect(m_editor, &VMdEditor::requestTextToHtml,
            this, &VMdTab::textToHtmlViaWebView);
    connect(m_editor, &VMdEditor::requestHtmlToText,
            this, &VMdTab::htmlToTextViaWebView);

    connect(m_editor, &VMdEditor::requestUploadImageToGithub,
            this, &VMdTab::handleUploadImageToGithubRequested);
    connect(m_editor, &VMdEditor::requestUploadImageToGitee,
            this, &VMdTab::handleUploadImageToGiteeRequested);
    connect(m_editor, &VMdEditor::requestUploadImageToWechat,
            this, &VMdTab::handleUploadImageToWechatRequested);
    connect(m_editor, &VMdEditor::requestUploadImageToTencent,
            this, &VMdTab::handleUploadImageToTencentRequested);

    if (m_editor->getVim()) {
        connect(m_editor->getVim(), &VVim::commandLineTriggered,
                this, [this](VVim::CommandLineType p_type) {
                    if (m_isEditMode) {
                        emit triggerVimCmd(p_type);
                    }
                });
    }

    enableHeadingSequence(m_enableHeadingSequence);
    m_editor->reloadFile();
    m_splitter->insertWidget(0, m_editor);

    m_livePreviewHelper = new VLivePreviewHelper(m_editor, m_document, this);
    connect(m_editor->getMarkdownHighlighter(), &PegMarkdownHighlighter::codeBlocksUpdated,
            m_livePreviewHelper, &VLivePreviewHelper::updateCodeBlocks);
    connect(m_editor->getPreviewManager(), &VPreviewManager::previewEnabledChanged,
            m_livePreviewHelper, &VLivePreviewHelper::setInplacePreviewEnabled);
    connect(m_livePreviewHelper, &VLivePreviewHelper::inplacePreviewCodeBlockUpdated,
            m_editor->getPreviewManager(), &VPreviewManager::updateCodeBlocks);
    connect(m_livePreviewHelper, &VLivePreviewHelper::checkBlocksForObsoletePreview,
            m_editor->getPreviewManager(), &VPreviewManager::checkBlocksForObsoletePreview);
    m_livePreviewHelper->setInplacePreviewEnabled(m_editor->getPreviewManager()->isPreviewEnabled());

    m_mathjaxPreviewHelper = new VMathJaxInplacePreviewHelper(m_editor, m_document, this);
    connect(m_editor->getMarkdownHighlighter(), &PegMarkdownHighlighter::mathjaxBlocksUpdated,
            m_mathjaxPreviewHelper, &VMathJaxInplacePreviewHelper::updateMathjaxBlocks);
    connect(m_editor->getPreviewManager(), &VPreviewManager::previewEnabledChanged,
            m_mathjaxPreviewHelper, &VMathJaxInplacePreviewHelper::setEnabled);
    connect(m_mathjaxPreviewHelper, &VMathJaxInplacePreviewHelper::inplacePreviewMathjaxBlockUpdated,
            m_editor->getPreviewManager(), &VPreviewManager::updateMathjaxBlocks);
    connect(m_mathjaxPreviewHelper, &VMathJaxInplacePreviewHelper::checkBlocksForObsoletePreview,
            m_editor->getPreviewManager(), &VPreviewManager::checkBlocksForObsoletePreview);
    m_mathjaxPreviewHelper->setEnabled(m_editor->getPreviewManager()->isPreviewEnabled());

    vGithubImageHosting->setEditor(m_editor);
    vGiteeImageHosting->setEditor(m_editor);
    vWechatImageHosting->setEditor(m_editor);
    vTencentImageHosting->setEditor(m_editor);
}

void VMdTab::updateOutlineFromHtml(const QString &p_tocHtml)
{
    if (m_isEditMode) {
        return;
    }

    m_outline.clear();

    if (m_outline.parseTableFromHtml(p_tocHtml)) {
        m_outline.setFile(m_file);
        m_outline.setType(VTableOfContentType::Anchor);
    }

    m_currentHeader.reset();

    emit outlineChanged(m_outline);
}

void VMdTab::updateOutlineFromHeaders(const QVector<VTableOfContentItem> &p_headers)
{
    if (!m_isEditMode) {
        return;
    }

    m_outline.update(m_file,
                     p_headers,
                     VTableOfContentType::BlockNumber);

    m_currentHeader.reset();

    emit outlineChanged(m_outline);
}

void VMdTab::scrollToHeader(const VHeaderPointer &p_header)
{
    if (m_outline.isMatched(p_header)) {
        // Scroll only when @p_header is valid.
        scrollToHeaderInternal(p_header);
    }
}

void VMdTab::updateCurrentHeader(const QString &p_anchor)
{
    if (m_isEditMode) {
        return;
    }

    // Find the index of the anchor in outline.
    int idx = m_outline.indexOfItemByAnchor(p_anchor);
    m_currentHeader.update(m_file, idx);

    emit currentHeaderChanged(m_currentHeader);
}

void VMdTab::updateCurrentHeader(int p_blockNumber)
{
    if (!m_isEditMode) {
        return;
    }

    // Find the index of the block number in outline.
    int idx = m_outline.indexOfItemByBlockNumber(p_blockNumber);
    m_currentHeader.update(m_file, idx);

    emit currentHeaderChanged(m_currentHeader);
}

void VMdTab::insertImage()
{
    if (!m_isEditMode) {
        return;
    }

    Q_ASSERT(m_editor);
    m_editor->insertImage();
}

void VMdTab::insertLink()
{
    if (!m_isEditMode) {
        return;
    }

    Q_ASSERT(m_editor);
    m_editor->insertLink();
}

void VMdTab::insertTable()
{
    if (!m_isEditMode) {
        return;
    }

    Q_ASSERT(m_editor);
    m_editor->insertTable();
}

void VMdTab::findText(const QString &p_text, uint p_options, bool p_peek,
                      bool p_forward)
{
    if (m_isEditMode && !previewExpanded()) {
        if (p_peek) {
            m_editor->peekText(p_text, p_options);
        } else {
            m_editor->findText(p_text, p_options, p_forward);
        }
    } else {
        findTextInWebView(p_text, p_options, p_peek, p_forward);
    }
}

void VMdTab::findText(const VSearchToken &p_token,
                      bool p_forward,
                      bool p_fromStart)
{
    if (m_isEditMode) {
        m_editor->findText(p_token, p_forward, p_fromStart);
    } else {
        // TODO
        Q_ASSERT(false);
    }
}

void VMdTab::replaceText(const QString &p_text, uint p_options,
                         const QString &p_replaceText, bool p_findNext)
{
    if (m_isEditMode) {
        Q_ASSERT(m_editor);
        m_editor->replaceText(p_text, p_options, p_replaceText, p_findNext);
    }
}

void VMdTab::replaceTextAll(const QString &p_text, uint p_options,
                            const QString &p_replaceText)
{
    if (m_isEditMode) {
        Q_ASSERT(m_editor);
        m_editor->replaceTextAll(p_text, p_options, p_replaceText);
    }
}

void VMdTab::nextMatch(const QString &p_text, uint p_options, bool p_forward)
{
    if (m_isEditMode) {
        Q_ASSERT(m_editor);
        m_editor->nextMatch(p_forward);
    } else {
        findTextInWebView(p_text, p_options, false, p_forward);
    }
}

void VMdTab::findTextInWebView(const QString &p_text, uint p_options,
                               bool /* p_peek */, bool p_forward)
{
    V_ASSERT(m_webViewer);

    QWebEnginePage::FindFlags flags;
    if (p_options & FindOption::CaseSensitive) {
        flags |= QWebEnginePage::FindCaseSensitively;
    }

    if (!p_forward) {
        flags |= QWebEnginePage::FindBackward;
    }

    m_webViewer->findText(p_text, flags);
}

QString VMdTab::getSelectedText() const
{
    if (m_isEditMode) {
        Q_ASSERT(m_editor);
        QTextCursor cursor = m_editor->textCursor();
        return cursor.selectedText();
    } else {
        return m_webViewer->selectedText();
    }
}

void VMdTab::clearSearchedWordHighlight()
{
    if (m_webViewer) {
        m_webViewer->findText("");
    }

    if (m_editor) {
        m_editor->clearSearchedWordHighlight();
    }
}

void VMdTab::handleWebKeyPressed(int p_key, bool p_ctrl, bool p_shift, bool p_meta)
{
    V_ASSERT(m_webViewer);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    bool macCtrl = p_meta;
#else
    Q_UNUSED(p_meta);
    bool macCtrl = false;
#endif

    switch (p_key) {
    // Esc
    case 27:
        m_editArea->getFindReplaceDialog()->closeDialog();
        break;

    // Dash
    case 189:
        if (p_ctrl || macCtrl) {
            // Zoom out.
            zoomWebPage(false);
        }

        break;

    // Equal
    case 187:
        if (p_ctrl || macCtrl) {
            // Zoom in.
            zoomWebPage(true);
        }

        break;

    // 0
    case 48:
        if (p_ctrl || macCtrl) {
            // Recover zoom.
            m_webViewer->setZoomFactor(1);
        }

        break;

    // / or ?
    case 191:
        if (!p_ctrl) {
            VVim::CommandLineType type = VVim::CommandLineType::SearchForward;
            if (p_shift) {
                // ?, search backward.
                type = VVim::CommandLineType::SearchBackward;
            }

            emit triggerVimCmd(type);
        }

        break;

    // :
    case 186:
        if (!p_ctrl && p_shift) {
            VVim::CommandLineType type = VVim::CommandLineType::Command;

            emit triggerVimCmd(type);
        }

        break;

    // n or N
    case 78:
        if (!p_ctrl) {
            if (!m_lastSearchItem.isEmpty()) {
                bool forward = !p_shift;
                findTextInWebView(m_lastSearchItem.m_text,
                                  m_lastSearchItem.m_options,
                                  false,
                                  forward ? m_lastSearchItem.m_forward : !m_lastSearchItem.m_forward);
            }
        }

        break;

    default:
        break;
    }
}

void VMdTab::zoom(bool p_zoomIn, qreal p_step)
{
    if (!m_isEditMode || m_mode == Mode::EditPreview) {
        zoomWebPage(p_zoomIn, p_step);
    }
}

void VMdTab::zoomWebPage(bool p_zoomIn, qreal p_step)
{
    V_ASSERT(m_webViewer);

    qreal curFactor = m_webViewer->zoomFactor();
    qreal newFactor = p_zoomIn ? curFactor + p_step : curFactor - p_step;
    if (newFactor < c_webZoomFactorMin) {
        newFactor = c_webZoomFactorMin;
    } else if (newFactor > c_webZoomFactorMax) {
        newFactor = c_webZoomFactorMax;
    }

    m_webViewer->setZoomFactor(newFactor);
}

VWebView *VMdTab::getWebViewer() const
{
    return m_webViewer;
}

MarkdownConverterType VMdTab::getMarkdownConverterType() const
{
    return m_mdConType;
}

void VMdTab::focusChild()
{
    switch (m_mode) {
    case Mode::Read:
        m_webViewer->setFocus();
        break;

    case Mode::Edit:
        m_editor->setFocus();
        break;

    case Mode::EditPreview:
        if (m_editor->isVisible()) {
            m_editor->setFocus();
        } else {
            m_webViewer->setFocus();
        }

        break;

    default:
        this->setFocus();
        break;
    }
}

void VMdTab::requestUpdateVimStatus()
{
    if (m_editor) {
        m_editor->requestUpdateVimStatus();
    } else {
        emit vimStatusUpdated(NULL);
    }
}

VEditTabInfo VMdTab::fetchTabInfo(VEditTabInfo::InfoType p_type) const
{
    VEditTabInfo info = VEditTab::fetchTabInfo(p_type);

    if (m_editor) {
        QTextCursor cursor = m_editor->textCursor();
        info.m_cursorBlockNumber = cursor.block().blockNumber();
        info.m_cursorPositionInBlock = cursor.positionInBlock();
        info.m_blockCount = m_editor->document()->blockCount();
    }

    if (m_isEditMode) {
        if (m_editor) {
            // We do not get the full word count info in edit mode.
            info.m_wordCountInfo.m_mode = VWordCountInfo::Edit;
            info.m_wordCountInfo.m_charWithSpacesCount = m_editor->document()->characterCount() - 1;
        }
    } else {
        info.m_wordCountInfo = m_document->getWordCountInfo();
    }

    info.m_headerIndex = m_currentHeader.m_index;

    return info;
}

void VMdTab::decorateText(TextDecoration p_decoration, int p_level)
{
    if (m_editor) {
        m_editor->decorateText(p_decoration, p_level);
    }
}

bool VMdTab::restoreFromTabInfo(const VEditTabInfo &p_info)
{
    if (p_info.m_editTab != this) {
        return false;
    }

    bool ret = false;
    // Restore cursor position.
    if (m_isEditMode
        && m_editor
        && p_info.m_cursorBlockNumber > -1
        && p_info.m_cursorPositionInBlock > -1) {
        ret = m_editor->setCursorPosition(p_info.m_cursorBlockNumber, p_info.m_cursorPositionInBlock);
    }

    // Restore header.
    if (!ret) {
        VHeaderPointer header(m_file, p_info.m_headerIndex);
        ret = scrollToHeaderInternal(header);
    }

    return ret;
}

void VMdTab::restoreFromTabInfo()
{
    restoreFromTabInfo(m_infoToRestore);

    // Clear it anyway.
    m_infoToRestore.clear();
}

void VMdTab::enableHeadingSequence(bool p_enabled)
{
    m_enableHeadingSequence = p_enabled;

    if (m_editor) {
        VEditConfig &config = m_editor->getConfig();
        config.m_enableHeadingSequence = m_enableHeadingSequence;
        if (isEditMode()) {
            m_editor->updateHeaderSequenceByConfigChange();
        }
    }
}

bool VMdTab::isHeadingSequenceEnabled() const
{
    return m_enableHeadingSequence;
}

void VMdTab::evaluateMagicWords()
{
    if (isEditMode() && m_file->isModifiable()) {
        getEditor()->evaluateMagicWords();
    }
}

void VMdTab::applySnippet(const VSnippet *p_snippet)
{
    Q_ASSERT(p_snippet);

    if (isEditMode()
        && m_file->isModifiable()
        && p_snippet->getType() == VSnippet::Type::PlainText) {
        Q_ASSERT(m_editor);
        QTextCursor cursor = m_editor->textCursor();
        bool changed = p_snippet->apply(cursor);
        if (changed) {
            m_editor->setTextCursor(cursor);

            m_editor->setVimMode(VimMode::Insert);

            g_mainWin->showStatusMessage(tr("Snippet applied"));

            focusTab();
        }
    } else {
        g_mainWin->showStatusMessage(tr("Snippet %1 is not applicable").arg(p_snippet->getName()));
    }
}

void VMdTab::applySnippet()
{
    if (!isEditMode() || !m_file->isModifiable()) {
        g_mainWin->showStatusMessage(tr("Snippets are not applicable"));
        return;
    }

    QPoint pos(m_editor->cursorRect().bottomRight());
    QMenu menu(this);
    VInsertSelector *sel = prepareSnippetSelector(&menu);
    if (!sel) {
        g_mainWin->showStatusMessage(tr("No available snippets defined with shortcuts"));
        return;
    }

    QWidgetAction *act = new QWidgetAction(&menu);
    act->setDefaultWidget(sel);
    connect(sel, &VInsertSelector::accepted,
            this, [&menu]() {
                QKeyEvent *escEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape,
                                                    Qt::NoModifier);
                QCoreApplication::postEvent(&menu, escEvent);
            });

    menu.addAction(act);

    menu.exec(m_editor->mapToGlobal(pos));

    QString chosenItem = sel->getClickedItem();
    if (!chosenItem.isEmpty()) {
        const VSnippet *snip = g_mainWin->getSnippetList()->getSnippet(chosenItem);
        if (snip) {
            applySnippet(snip);
        }
    }
}

static bool selectorItemCmp(const VInsertSelectorItem &p_a, const VInsertSelectorItem &p_b)
{
    if (p_a.m_shortcut < p_b.m_shortcut) {
        return true;
    }

    return false;
}

VInsertSelector *VMdTab::prepareSnippetSelector(QWidget *p_parent)
{
    auto snippets = g_mainWin->getSnippetList()->getSnippets();
    QVector<VInsertSelectorItem> items;
    for (auto const & snip : snippets) {
        if (!snip.getShortcut().isNull()) {
            items.push_back(VInsertSelectorItem(snip.getName(),
                                                snip.getName(),
                                                snip.getShortcut()));
        }
    }

    if (items.isEmpty()) {
        return NULL;
    }

    // Sort items by shortcut.
    std::sort(items.begin(), items.end(), selectorItemCmp);

    VInsertSelector *sel = new VInsertSelector(7, items, p_parent);
    return sel;
}

void VMdTab::reload()
{
    // Reload editor.
    if (m_editor) {
        m_editor->reloadFile();
    }

    if (m_isEditMode) {
        m_editor->endEdit();
        m_editor->beginEdit();
        updateStatus();
    }

    if (!m_isEditMode) {
        updateWebView();
    }

    // Reload web viewer.
    m_ready &= ~TabReady::ReadMode;
    m_webViewer->reload();

    if (!m_isEditMode) {
        VUtils::sleepWait(500);
        showFileReadMode();
    }
}

void VMdTab::tabIsReady(TabReady p_mode)
{
    bool isCurrentMode = (m_isEditMode && p_mode == TabReady::EditMode)
                         || (!m_isEditMode && p_mode == TabReady::ReadMode);

    if (isCurrentMode) {
        if (p_mode == TabReady::ReadMode) {
            m_document->muteWebView(false);
        }

        restoreFromTabInfo();

        if (m_enableBackupFile
            && !m_backupFileChecked
            && m_file->isModifiable()) {
            if (!checkPreviousBackupFile()) {
                return;
            }
        }
    }

    if (m_enableBackupFile
        && m_file->isModifiable()
        && p_mode == TabReady::EditMode) {
        // contentsChanged will be emitted even the content is not changed.
        connect(m_editor->document(), &QTextDocument::contentsChange,
                this, [this]() {
                    if (m_isEditMode) {
                        m_backupTimer->stop();
                        m_backupTimer->start();
                    }
                });
    }

    if (m_editor
        && p_mode == TabReady::ReadMode
        && m_livePreviewHelper->isPreviewEnabled()) {
        // Need to re-preview.
        m_editor->getMarkdownHighlighter()->updateHighlight();
    }
}

void VMdTab::writeBackupFile()
{
    Q_ASSERT(m_enableBackupFile && m_file->isModifiable());
    m_file->writeBackupFile(m_editor->getContent());
}

bool VMdTab::checkPreviousBackupFile()
{
    m_backupFileChecked = true;

    QString preFile = m_file->backupFileOfPreviousSession();
    if (preFile.isEmpty()) {
        return true;
    }

    QString backupContent = m_file->readBackupFile(preFile);
    if (m_file->getContent() == backupContent) {
        // Found backup file with identical content.
        // Just discard the backup file.
        VUtils::deleteFile(preFile);
        return true;
    }

    QMessageBox box(QMessageBox::Warning,
                    tr("Backup File Found"),
                    tr("Found backup file <span style=\"%1\">%2</span> "
                       "when opening note <span style=\"%1\">%3</span>.")
                      .arg(g_config->c_dataTextStyle)
                      .arg(preFile)
                      .arg(m_file->fetchPath()),
                    QMessageBox::NoButton,
                    this);
    QString info = tr("VNote may crash while editing this note before.<br/>"
                      "Please choose to recover from the backup file or delete it.<br/><br/>"
                      "Note file last modified: <span style=\"%1\">%2</span><br/>"
                      "Backup file last modified: <span style=\"%1\">%3</span>")
                     .arg(g_config->c_dataTextStyle)
                     .arg(VUtils::displayDateTime(QFileInfo(m_file->fetchPath()).lastModified()))
                     .arg(VUtils::displayDateTime(QFileInfo(preFile).lastModified()));
    box.setInformativeText(info);
    QPushButton *recoverBtn = box.addButton(tr("Recover From Backup File"), QMessageBox::YesRole);
    box.addButton(tr("Discard Backup File"), QMessageBox::NoRole);
    QPushButton *cancelBtn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);

    box.setDefaultButton(cancelBtn);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);

    box.exec();
    QAbstractButton *btn = box.clickedButton();
    if (btn == cancelBtn || !btn) {
        // Close current tab.
        emit closeRequested(this);
        return false;
    } else if (btn == recoverBtn) {
        // Load content from the backup file.
        if (!m_isEditMode) {
            showFileEditMode();
        }

        Q_ASSERT(m_editor);
        m_editor->setContent(backupContent, true);

        updateStatus();
    }

    VUtils::deleteFile(preFile);

    return true;
}

void VMdTab::updateCursorStatus()
{
    emit statusUpdated(fetchTabInfo(VEditTabInfo::InfoType::Cursor));
}

void VMdTab::handleFileOrDirectoryChange(bool p_isFile, UpdateAction p_act)
{
    // Reload the web view with new base URL.
    m_headerFromEditMode = m_currentHeader;
    m_webViewer->setHtml(VUtils::generateHtmlTemplate(m_mdConType),
                         m_file->getBaseUrl());

    if (m_editor) {
        m_editor->updateInitAndInsertedImages(p_isFile, p_act);

        // Refresh the previewed images in edit mode.
        m_editor->refreshPreview();
    }
}

void VMdTab::textToHtmlViaWebView(const QString &p_text, int p_id, int p_timeStamp)
{
    int maxRetry = 50;
    while (!m_document->isReadyToTextToHtml() && maxRetry > 0) {
        qDebug() << "wait for web side ready to convert text to HTML";
        VUtils::sleepWait(100);
        --maxRetry;
    }

    if (maxRetry == 0) {
        qWarning() << "web side is not ready to convert text to HTML";
        return;
    }

    m_document->textToHtmlAsync(m_documentID, p_id, p_timeStamp, p_text, true);
}

void VMdTab::htmlToTextViaWebView(const QString &p_html, int p_id, int p_timeStamp)
{
    int maxRetry = 50;
    while (!m_document->isReadyToTextToHtml() && maxRetry > 0) {
        qDebug() << "wait for web side ready to convert HTML to text";
        VUtils::sleepWait(100);
        --maxRetry;
    }

    if (maxRetry == 0) {
        qWarning() << "web side is not ready to convert HTML to text";
        return;
    }

    m_document->htmlToTextAsync(m_documentID, p_id, p_timeStamp, p_html);
}

void VMdTab::handleVimCmdCommandCancelled()
{
    if (m_isEditMode) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            vim->processCommandLineCancelled();
        }
    }
}

void VMdTab::handleVimCmdCommandFinished(VVim::CommandLineType p_type, const QString &p_cmd)
{
    if (m_isEditMode && !previewExpanded()) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            vim->processCommandLine(p_type, p_cmd);
        }
    } else {
        if (p_type == VVim::CommandLineType::SearchForward
            || p_type == VVim::CommandLineType::SearchBackward) {
            m_lastSearchItem = VVim::fetchSearchItem(p_type, p_cmd);
            findTextInWebView(m_lastSearchItem.m_text,
                              m_lastSearchItem.m_options,
                              false,
                              m_lastSearchItem.m_forward);
        } else {
            executeVimCommandInWebView(p_cmd);
        }
    }
}

void VMdTab::handleVimCmdCommandChanged(VVim::CommandLineType p_type, const QString &p_cmd)
{
    Q_UNUSED(p_type);
    Q_UNUSED(p_cmd);
    if (m_isEditMode && !previewExpanded()) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            vim->processCommandLineChanged(p_type, p_cmd);
        }
    } else {
        if (p_type == VVim::CommandLineType::SearchForward
            || p_type == VVim::CommandLineType::SearchBackward) {
            VVim::SearchItem item = VVim::fetchSearchItem(p_type, p_cmd);
            findTextInWebView(item.m_text, item.m_options, true, item.m_forward);
        }
    }
}

QString VMdTab::handleVimCmdRequestNextCommand(VVim::CommandLineType p_type, const QString &p_cmd)
{
    Q_UNUSED(p_type);
    Q_UNUSED(p_cmd);
    if (m_isEditMode) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            return vim->getNextCommandHistory(p_type, p_cmd);
        }
    }

    return QString();
}

QString VMdTab::handleVimCmdRequestPreviousCommand(VVim::CommandLineType p_type, const QString &p_cmd)
{
    Q_UNUSED(p_type);
    Q_UNUSED(p_cmd);
    if (m_isEditMode) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            return vim->getPreviousCommandHistory(p_type, p_cmd);
        }
    }

    return QString();
}

QString VMdTab::handleVimCmdRequestRegister(int p_key, int p_modifiers)
{
    Q_UNUSED(p_key);
    Q_UNUSED(p_modifiers);
    if (m_isEditMode) {
        VVim *vim = getEditor()->getVim();
        if (vim) {
            return vim->readRegister(p_key, p_modifiers);
        }
    }

    return QString();
}

bool VMdTab::executeVimCommandInWebView(const QString &p_cmd)
{
    bool validCommand = true;
    QString msg;

    if (p_cmd.isEmpty()) {
        return true;
    } else if (p_cmd == "q") {
        // :q, close the note.
        emit closeRequested(this);
        msg = tr("Quit");
    } else if (p_cmd == "nohlsearch" || p_cmd == "noh") {
        // :nohlsearch, clear highlight search.
        m_webViewer->findText("");
    } else if (p_cmd == "e") {
        // :e, enter edit mode.
        showFileEditMode();
    } else if (p_cmd.size() > 2 && p_cmd.left(2) == "e ") {
        // :e <file>, open a note in edit mode.
        auto filePath = p_cmd.mid(2).trimmed();

        if(filePath.left(2) == "~/") {
            filePath.remove(0, 1).insert(0, QDir::homePath());
        } else if(filePath.left(2) == "./" || filePath[0] != '/') {
            VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
            if(filePath.left(2) == "./") {
                filePath.remove(0, 1);
            } else {
                filePath.insert(0, '/');
            }
            filePath.insert(0, dir->fetchPath());
        }

        if(g_mainWin->openFiles(QStringList(filePath), false, OpenFileMode::Edit).size()) {
            msg = tr("Open %1 in edit mode.").arg(filePath);
        } else {
            msg = tr("Unable to open %1").arg(filePath);
        }
    } else {
        validCommand = false;
    }

    if (!validCommand) {
        g_mainWin->showStatusMessage(tr("Not an editor command: %1").arg(p_cmd));
    } else {
        g_mainWin->showStatusMessage(msg);
    }

    return validCommand;
}

void VMdTab::handleDownloadRequested(QWebEngineDownloadItem *p_item)
{
    connect(p_item, &QWebEngineDownloadItem::stateChanged,
            this, [p_item, this](QWebEngineDownloadItem::DownloadState p_state) {
                QString msg;
                switch (p_state) {
                case QWebEngineDownloadItem::DownloadCompleted:
                    emit statusMessage(tr("Page saved to %1").arg(p_item->path()));
                    break;

                case QWebEngineDownloadItem::DownloadCancelled:
                case QWebEngineDownloadItem::DownloadInterrupted:
                    emit statusMessage(tr("Fail to save page to %1").arg(p_item->path()));
                    break;

                default:
                    break;
                }
            });
}

void VMdTab::handleSavePageRequested()
{
    static QString lastPath = g_config->getDocumentPathOrHomePath();

    QStringList filters;
    filters << tr("Single HTML (*.html)") << tr("Complete HTML (*.html)") << tr("MIME HTML (*.mht)");
    QList<QWebEngineDownloadItem::SavePageFormat> formats;
    formats << QWebEngineDownloadItem::SingleHtmlSaveFormat
            << QWebEngineDownloadItem::CompleteHtmlSaveFormat
            << QWebEngineDownloadItem::MimeHtmlSaveFormat;

    QString selectedFilter = filters[1];
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Page"),
                                                    lastPath,
                                                    filters.join(";;"),
                                                    &selectedFilter);
    if (fileName.isEmpty()) {
        return;
    }

    lastPath = QFileInfo(fileName).path();

    QWebEngineDownloadItem::SavePageFormat format = formats.at(filters.indexOf(selectedFilter));

    qDebug() << "save page as" << format << "to" << fileName;

    emit statusMessage(tr("Saving page to %1").arg(fileName));

    m_webViewer->page()->save(fileName, format);
}

void VMdTab::handleUploadImageToGithubRequested()
{
    if (isModified()) {
        VUtils::showMessage(QMessageBox::Information,
                            tr("Information"),
                            tr("Please save changes to file before uploading images."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    vGithubImageHosting->handleUploadImageToGithubRequested();
}

void VMdTab::handleUploadImageToGiteeRequested()
{
    if (isModified()) {
        VUtils::showMessage(QMessageBox::Information,
                            tr("Information"),
                            tr("Please save changes to file before uploading images."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    vGiteeImageHosting->handleUploadImageToGiteeRequested();
}

void VMdTab::handleUploadImageToWechatRequested()
{
    if (isModified()) {
        VUtils::showMessage(QMessageBox::Information,
                            tr("Information"),
                            tr("Please save changes to file before uploading images."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    vWechatImageHosting->handleUploadImageToWechatRequested();
}

void VMdTab::handleUploadImageToTencentRequested()
{
    if (isModified()) {
        VUtils::showMessage(QMessageBox::Information,
                            tr("Information"),
                            tr("Please save changes to file before uploading images."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    vTencentImageHosting->handleUploadImageToTencentRequested();
}

VWordCountInfo VMdTab::fetchWordCountInfo(bool p_editMode) const
{
    if (p_editMode) {
        if (m_editor) {
            return m_editor->fetchWordCountInfo();
        }
    } else {
        // Request to update with current text.
        if (m_isEditMode) {
            const_cast<VMdTab *>(this)->updateWebView();
        }

        return m_document->getWordCountInfo();
    }

    return VWordCountInfo();
}

void VMdTab::setCurrentMode(Mode p_mode)
{
    if (m_mode == p_mode) {
        return;
    }

    qreal factor = m_webViewer->zoomFactor();
    if (m_mode == Mode::Read) {
        m_readWebViewState->m_zoomFactor = factor;
    } else if (m_mode == Mode::EditPreview) {
        m_previewWebViewState->m_zoomFactor = factor;
        m_livePreviewHelper->setLivePreviewEnabled(false);
    }

    m_mode = p_mode;

    switch (p_mode) {
    case Mode::Read:
        if (m_editor) {
            m_editor->hide();
        }

        m_webViewer->setInPreview(false);
        m_webViewer->show();

        // Fix the bug introduced by 051088be31dbffa3c04e2d382af15beec40d5fdb
        // which replace QStackedLayout with QSplitter.
        QCoreApplication::sendPostedEvents();

        if (m_readWebViewState.isNull()) {
            m_readWebViewState.reset(new WebViewState());
            m_readWebViewState->m_zoomFactor = factor;
        } else if (factor != m_readWebViewState->m_zoomFactor) {
            m_webViewer->setZoomFactor(m_readWebViewState->m_zoomFactor);
        }

        m_document->setPreviewEnabled(false);
        break;

    case Mode::Edit:
        m_document->muteWebView(true);
        m_webViewer->hide();
        m_editor->show();

        QCoreApplication::sendPostedEvents();

        break;

    case Mode::EditPreview:
        Q_ASSERT(m_editor);
        m_document->muteWebView(true);
        m_webViewer->setInPreview(true);
        m_webViewer->show();
        m_editor->show();

        QCoreApplication::sendPostedEvents();

        if (m_previewWebViewState.isNull()) {
            m_previewWebViewState.reset(new WebViewState());
            m_previewWebViewState->m_zoomFactor = factor;

            // Init the size of two splits.
            QList<int> sizes = m_splitter->sizes();
            Q_ASSERT(sizes.size() == 2);
            int a = (sizes[0] + sizes[1]) / 2;
            if (a <= 0) {
                a = 1;
            }

            int b = (sizes[0] + sizes[1]) - a;

            QList<int> newSizes;
            newSizes.append(a);
            newSizes.append(b);
            m_splitter->setSizes(newSizes);
        } else if (factor != m_previewWebViewState->m_zoomFactor) {
            m_webViewer->setZoomFactor(m_previewWebViewState->m_zoomFactor);
        }

        m_document->setPreviewEnabled(true);
        m_livePreviewHelper->setLivePreviewEnabled(true);
        m_editor->getMarkdownHighlighter()->updateHighlight();
        break;

    default:
        break;
    }

    focusChild();
}

bool VMdTab::toggleLivePreview()
{
    bool ret = false;

    switch (m_mode) {
    case Mode::EditPreview:
        setCurrentMode(Mode::Edit);
        ret = true;
        break;

    case Mode::Edit:
        setCurrentMode(Mode::EditPreview);
        ret = true;
        break;

    default:
        break;
    }

    return ret;
}

void VMdTab::handleWebSelectionChanged()
{
    if (m_mode != Mode::EditPreview
        || !(g_config->getSmartLivePreview() & SmartLivePreview::WebToEditor)) {
        return;
    }

    m_livePreviewTimer->start();
}

bool VMdTab::expandRestorePreviewArea()
{
    if (m_mode != Mode::EditPreview) {
        return false;
    }

    if (m_editor->isVisible()) {
        m_editor->hide();
        m_webViewer->setFocus();
    } else {
        m_editor->show();
        m_editor->setFocus();
    }

    return true;
}

bool VMdTab::previewExpanded() const
{
    return (m_mode == Mode::EditPreview) && !m_editor->isVisible();
}
