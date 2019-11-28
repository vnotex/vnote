#include "vmdeditor.h"

#include <QtWidgets>
#include <QMenu>
#include <QDebug>
#include <QScopedPointer>
#include <QClipboard>
#include <QMimeDatabase>
#include <QTemporaryFile>
#include <QProgressDialog>

#include "vdocument.h"
#include "utils/veditutils.h"
#include "vedittab.h"
#include "pegmarkdownhighlighter.h"
#include "vcodeblockhighlighthelper.h"
#include "vmdeditoperations.h"
#include "vtableofcontent.h"
#include "utils/veditutils.h"
#include "dialog/vselectdialog.h"
#include "dialog/vconfirmdeletiondialog.h"
#include "vtextblockdata.h"
#include "vorphanfile.h"
#include "vnotefile.h"
#include "vpreviewmanager.h"
#include "utils/viconutils.h"
#include "dialog/vcopytextashtmldialog.h"
#include "utils/vwebutils.h"
#include "dialog/vinsertlinkdialog.h"
#include "utils/vclipboardutils.h"
#include "vplantumlhelper.h"
#include "vgraphvizhelper.h"
#include "vmdtab.h"
#include "vdownloader.h"
#include "vtablehelper.h"
#include "dialog/vinserttabledialog.h"

extern VWebUtils *g_webUtils;

extern VConfigManager *g_config;

#define LINE_NUMBER_AREA_FONT_DELTA -2

VMdEditor::VMdEditor(VFile *p_file,
                     VDocument *p_doc,
                     MarkdownConverterType p_type,
                     const QSharedPointer<VTextEditCompleter> &p_completer,
                     QWidget *p_parent)
    : VTextEdit(p_parent),
      VEditor(p_file, this, p_completer),
      m_pegHighlighter(NULL),
      m_freshEdit(true),
      m_textToHtmlDialog(NULL),
      m_zoomDelta(0),
      m_editTab(NULL),
      m_copyTimeStamp(0)
{
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);

    VEditor::init();

    // Hook functions from VEditor.
    connect(this, &VTextEdit::cursorPositionChanged,
            this, [this]() {
                highlightOnCursorPositionChanged();
            });

    connect(document(), &QTextDocument::contentsChange,
            this, [this](int p_position, int p_charsRemoved, int p_charsAdded) {
                Q_UNUSED(p_position);
                if (p_charsRemoved > 0 || p_charsAdded > 0) {
                    updateTimeStamp();
                }
            });

    connect(this, &VTextEdit::selectionChanged,
            this, [this]() {
                highlightSelectedWord();
            });
    // End.

    setReadOnly(true);

    m_pegHighlighter = new PegMarkdownHighlighter(document(), this);
    m_pegHighlighter->init(g_config->getMdHighlightingStyles(),
                           g_config->getCodeBlockStyles(),
                           g_config->getEnableMathjax(),
                           g_config->getMarkdownHighlightInterval());
    connect(m_pegHighlighter, &PegMarkdownHighlighter::headersUpdated,
            this, &VMdEditor::updateHeaders);

    // After highlight, the cursor may trun into non-visible. We should make it visible
    // in this case.
    connect(m_pegHighlighter, &PegMarkdownHighlighter::highlightCompleted,
            this, [this]() {
            scrollCursorLineIfNecessary();

            if (m_freshEdit) {
                m_freshEdit = false;
                emit m_object->ready();
            }
    });

    m_cbHighlighter = new VCodeBlockHighlightHelper(m_pegHighlighter,
                                                    p_doc,
                                                    p_type);

    m_previewMgr = new VPreviewManager(this, m_pegHighlighter);
    connect(m_pegHighlighter, &PegMarkdownHighlighter::imageLinksUpdated,
            m_previewMgr, &VPreviewManager::updateImageLinks);
    connect(m_previewMgr, &VPreviewManager::requestUpdateImageLinks,
            m_pegHighlighter, &PegMarkdownHighlighter::updateHighlight);

    m_tableHelper = new VTableHelper(this);
    connect(m_pegHighlighter, &PegMarkdownHighlighter::tableBlocksUpdated,
            m_tableHelper, &VTableHelper::updateTableBlocks);

    m_editOps = new VMdEditOperations(this, m_file);
    connect(m_editOps, &VEditOperations::statusMessage,
            m_object, &VEditorObject::statusMessage);
    connect(m_editOps, &VEditOperations::vimStatusUpdated,
            m_object, &VEditorObject::vimStatusUpdated);

    connect(this, &VTextEdit::cursorPositionChanged,
            this, &VMdEditor::updateCurrentHeader);

    connect(this, &VTextEdit::cursorPositionChanged,
            m_object, &VEditorObject::cursorPositionChanged);

    setDisplayScaleFactor(VUtils::calculateScaleFactor());

    updateFontAndPalette();

    updateConfig();
}

void VMdEditor::updateFontAndPalette()
{
    QFont font(g_config->getMdEditFont());
    font.setPointSize(font.pointSize() + m_zoomDelta);
    setFont(font);

    const QPalette &palette = g_config->getMdEditPalette();

    /*
        Do not use this function in conjunction with Qt Style Sheets. When
        using style sheets, the palette of a widget can be customized using
        the "color", "background-color", "selection-color",
        "selection-background-color" and "alternate-background-color".
    */
    // setPalette(palette);
    // setTextColor(palette.color(QPalette::Text));

    // Only this could override the font-family set of QWidget in QSS.
    setFontAndPaletteByStyleSheet(font, palette);

    font.setPointSize(font.pointSize() + LINE_NUMBER_AREA_FONT_DELTA);
    updateLineNumberAreaWidth(QFontMetrics(font));
}

void VMdEditor::beginEdit()
{
    updateConfig();

    initInitImages();

    setModified(false);

    setReadOnlyAndHighlightCurrentLine(false);

    emit statusChanged();

    if (m_freshEdit) {
        m_pegHighlighter->updateHighlight();
        relayout();
    } else {
        updateHeaders(m_pegHighlighter->getHeaderRegions());
    }
}

void VMdEditor::endEdit()
{
    setReadOnlyAndHighlightCurrentLine(true);

    clearUnusedImages();
}

void VMdEditor::saveFile()
{
    Q_ASSERT(m_file->isModifiable());

    if (!document()->isModified()) {
        return;
    }

    m_file->setContent(toPlainText());
    setModified(false);

    clearUnusedImages();

    initInitImages();
}

void VMdEditor::reloadFile()
{
    bool readonly = isReadOnly();
    setReadOnly(true);

    const QString &content = m_file->getContent();
    setPlainText(content);
    setModified(false);

    setReadOnly(readonly);

    if (!m_freshEdit) {
        m_freshEdit = true;
        refreshPreview();
    }
}

bool VMdEditor::scrollToBlock(int p_blockNumber)
{
    QTextBlock block = document()->findBlockByNumber(p_blockNumber);
    if (block.isValid()) {
        scrollBlockInPage(block.blockNumber(), 0);
        moveCursor(QTextCursor::EndOfBlock);
        return true;
    }

    return false;
}

// Get the visual offset of a block.
#define GETVISUALOFFSETY(x) (contentOffsetY() + (int)(x).y())

void VMdEditor::makeBlockVisible(const QTextBlock &p_block)
{
    if (!p_block.isValid() || !p_block.isVisible()) {
        return;
    }

    QScrollBar *vbar = verticalScrollBar();
    if (!vbar || (vbar->minimum() == vbar->maximum())) {
        // No vertical scrollbar. No need to scroll.
        return;
    }

    int height = rect().height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && (hbar->minimum() != hbar->maximum())) {
        height -= hbar->height();
    }

    bool moved = false;

    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    QRectF rt = layout->blockBoundingRect(p_block);
    int y = GETVISUALOFFSETY(rt);
    int rectHeight = (int)rt.height();

    // Handle the case rectHeight >= height.
    if (rectHeight >= height) {
        if (y < 0) {
            // Need to scroll up.
            while (y + rectHeight < height && vbar->value() > vbar->minimum()) {
                moved = true;
                vbar->setValue(vbar->value() - vbar->singleStep());
                rt = layout->blockBoundingRect(p_block);
                rectHeight = (int)rt.height();
                y = GETVISUALOFFSETY(rt);
            }
        } else if (y > 0) {
            // Need to scroll down.
            while (y > 0 && vbar->value() < vbar->maximum()) {
                moved = true;
                vbar->setValue(vbar->value() + vbar->singleStep());
                rt = layout->blockBoundingRect(p_block);
                rectHeight = (int)rt.height();
                y = GETVISUALOFFSETY(rt);
            }

            if (y < 0) {
                // One step back.
                moved = true;
                vbar->setValue(vbar->value() - vbar->singleStep());
            }
        }

        return;
    }

    // There is an extra line leading in the layout, so there will always be a scroll
    // action to scroll the page down.
    while (y < 0 && vbar->value() > vbar->minimum()) {
        moved = true;
        vbar->setValue(vbar->value() - vbar->singleStep());
        rt = layout->blockBoundingRect(p_block);
        y = GETVISUALOFFSETY(rt);
    }

    if (moved) {
        return;
    }

    while (y + rectHeight > height && vbar->value() < vbar->maximum()) {
        vbar->setValue(vbar->value() + vbar->singleStep());
        rt = layout->blockBoundingRect(p_block);
        rectHeight = (int)rt.height();
        y = GETVISUALOFFSETY(rt);
    }
}

static QAction *getActionByObjectName(const QList<QAction *> &p_actions,
                                      const QString &p_objName)
{
    for (auto act : p_actions) {
        if (act->objectName() == p_objName) {
            return act;
        }
    }

    return NULL;
}

// Insert @p_action into @p_menu after action @p_after.
static void insertActionAfter(QAction *p_after, QAction *p_action, QMenu *p_menu)
{
    p_menu->insertAction(p_after, p_action);
    if (p_after) {
        p_menu->removeAction(p_after);
        p_menu->insertAction(p_action, p_after);
    }
}

static QAction *insertMenuAfter(QAction *p_after, QMenu *p_subMenu, QMenu *p_menu)
{
    QAction *menuAct = p_menu->insertMenu(p_after, p_subMenu);
    if (p_after) {
        p_menu->removeAction(p_after);
        p_menu->insertAction(menuAct, p_after);
    }

    return menuAct;
}

void VMdEditor::contextMenuEvent(QContextMenuEvent *p_event)
{
    if (!m_editTab || !m_editTab->isEditMode()) {
        return;
    }

    QScopedPointer<QMenu> menu(createStandardContextMenu());
    menu->setToolTipsVisible(true);

    QAction *copyAct, *pasteAct, *firstAct;
    {
    const QList<QAction *> actions = menu->actions();
    firstAct = actions.isEmpty() ? NULL : actions.first();
    copyAct = getActionByObjectName(actions, "edit-copy");
    pasteAct = getActionByObjectName(actions, "edit-paste");
    }

    if (copyAct && copyAct->isEnabled()) {
        initCopyAsMenu(copyAct, menu.data());
    }

    if (pasteAct && pasteAct->isEnabled()) {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            initPasteAsBlockQuoteMenu(pasteAct, menu.data());
        }

        if (mimeData->hasHtml()) {
            initPasteAfterParseMenu(pasteAct, menu.data());
        }

        QAction *pptAct = new QAction(tr("Paste As Plain Text"), menu.data());
        VUtils::fixTextWithShortcut(pptAct, "PastePlainText");
        connect(pptAct, &QAction::triggered,
                this, [this]() {
                    pastePlainText();
                });
        insertActionAfter(pasteAct, pptAct, menu.data());
    }

    if (!textCursor().hasSelection()) {
        initLinkAndPreviewMenu(firstAct, menu.data(), p_event->pos());

        QAction *saveExitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/save_exit.svg"),
                                           tr("&Save Changes And Read"),
                                           menu.data());
        saveExitAct->setToolTip(tr("Save changes and exit edit mode"));
        connect(saveExitAct, &QAction::triggered,
                this, [this]() {
                    emit m_object->saveAndRead();
                });

        QAction *discardExitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/discard_exit.svg"),
                                              tr("&Discard Changes And Read"),
                                              menu.data());
        discardExitAct->setToolTip(tr("Discard changes and exit edit mode"));
        connect(discardExitAct, &QAction::triggered,
                this, [this]() {
                    emit m_object->discardAndRead();
                });

        VMdTab *mdtab = dynamic_cast<VMdTab *>(m_editTab);
        if (mdtab) {
            QAction *toggleLivePreviewAct = new QAction(tr("Live Preview For Graphs"), menu.data());
            toggleLivePreviewAct->setToolTip(tr("Toggle live preview panel for graphs"));
            VUtils::fixTextWithCaptainShortcut(toggleLivePreviewAct, "LivePreview");
            connect(toggleLivePreviewAct, &QAction::triggered,
                    this, [mdtab]() {
                        mdtab->toggleLivePreview();
                    });

            menu->insertAction(firstAct, toggleLivePreviewAct);
            menu->insertAction(toggleLivePreviewAct, discardExitAct);
            menu->insertAction(discardExitAct, saveExitAct);
            menu->insertSeparator(toggleLivePreviewAct);
        } else {
            menu->insertAction(firstAct, discardExitAct);
            menu->insertAction(discardExitAct, saveExitAct);
            menu->insertSeparator(discardExitAct);
        }

        if (firstAct) {
            menu->insertSeparator(firstAct);
        }

        initAttachmentMenu(menu.data());

        initImageHostingMenu(menu.data());
    }

    menu->exec(p_event->globalPos());
}

void VMdEditor::mousePressEvent(QMouseEvent *p_event)
{
    if (handleMousePressEvent(p_event)) {
        return;
    }

    VTextEdit::mousePressEvent(p_event);

    emit m_object->mousePressed(p_event);
}

void VMdEditor::mouseDoubleClickEvent(QMouseEvent *p_event)
{
    VTextEdit::mouseDoubleClickEvent(p_event);

    emit m_object->mouseDoubleClicked(p_event);
}

void VMdEditor::mouseReleaseEvent(QMouseEvent *p_event)
{
    if (handleMouseReleaseEvent(p_event)) {
        return;
    }

    VTextEdit::mouseReleaseEvent(p_event);

    emit m_object->mouseReleased(p_event);
}

void VMdEditor::mouseMoveEvent(QMouseEvent *p_event)
{
    if (handleMouseMoveEvent(p_event)) {
        return;
    }

    VTextEdit::mouseMoveEvent(p_event);

    emit m_object->mouseMoved(p_event);
}

QVariant VMdEditor::inputMethodQuery(Qt::InputMethodQuery p_query) const
{
    QVariant ret;
    if (handleInputMethodQuery(p_query, ret)) {
        return ret;
    }

    return VTextEdit::inputMethodQuery(p_query);
}

bool VMdEditor::isBlockVisible(const QTextBlock &p_block)
{
    if (!p_block.isValid() || !p_block.isVisible()) {
        return false;
    }

    QScrollBar *vbar = verticalScrollBar();
    if (!vbar || !vbar->isVisible()) {
        // No vertical scrollbar.
        return true;
    }

    int height = rect().height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible()) {
        height -= hbar->height();
    }

    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    QRectF rt = layout->blockBoundingRect(p_block);
    int y = GETVISUALOFFSETY(rt);
    int rectHeight = (int)rt.height();

    return (y >= 0 && y < height) || (y < 0 && y + rectHeight > 0);
}

static void addHeaderSequence(QVector<int> &p_sequence, int p_level, int p_baseLevel)
{
    Q_ASSERT(p_level >= 1 && p_level < p_sequence.size());
    if (p_level < p_baseLevel) {
        p_sequence.fill(0);
        return;
    }

    ++p_sequence[p_level];
    for (int i = p_level + 1; i < p_sequence.size(); ++i) {
        p_sequence[i] = 0;
    }
}

static QString headerSequenceStr(const QVector<int> &p_sequence)
{
    QString res;
    for (int i = 1; i < p_sequence.size(); ++i) {
        if (p_sequence[i] != 0) {
            res = res + QString::number(p_sequence[i]) + '.';
        } else if (res.isEmpty()) {
            continue;
        } else {
            break;
        }
    }

    return res;
}

static void insertSequenceToHeader(QTextCursor& p_cursor,
                                   const QTextBlock &p_block,
                                   QRegExp &p_reg,
                                   QRegExp &p_preReg,
                                   const QString &p_seq)
{
    if (!p_block.isValid()) {
        return;
    }

    QString text = p_block.text();
    bool matched = p_reg.exactMatch(text);
    Q_ASSERT(matched);

    matched = p_preReg.exactMatch(text);
    Q_ASSERT(matched);

    int start = p_reg.cap(1).length() + 1;
    int end = p_preReg.cap(1).length();

    Q_ASSERT(start <= end);

    p_cursor.setPosition(p_block.position() + start);
    if (start != end) {
        p_cursor.setPosition(p_block.position() + end, QTextCursor::KeepAnchor);
    }

    if (p_seq.isEmpty()) {
        p_cursor.removeSelectedText();
    } else {
        p_cursor.insertText(p_seq + ' ');
    }
}

void VMdEditor::updateHeaderSequenceByConfigChange()
{
    updateHeadersHelper(m_pegHighlighter->getHeaderRegions(), true);
}

void VMdEditor::updateHeadersHelper(const QVector<VElementRegion> &p_headerRegions, bool p_configChanged)
{
    QTextDocument *doc = document();

    QVector<VTableOfContentItem> headers;
    QVector<int> headerBlockNumbers;
    QVector<QString> headerSequences;
    if (!p_headerRegions.isEmpty()) {
        headers.reserve(p_headerRegions.size());
        headerBlockNumbers.reserve(p_headerRegions.size());
        headerSequences.reserve(p_headerRegions.size());
    }

    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg(VUtils::c_headerRegExp);
    int baseLevel = -1;
    for (auto const & reg : p_headerRegions) {
        QTextBlock block = doc->findBlock(reg.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        if (!block.contains(reg.m_endPos - 1)) {
            qWarning() << "header accross multiple blocks, starting from block"
                       << block.blockNumber()
                       << block.text();
        }

        if (headerReg.exactMatch(block.text())) {
            int level = headerReg.cap(1).length();
            VTableOfContentItem header(headerReg.cap(2).trimmed(),
                                       level,
                                       block.blockNumber(),
                                       headers.size());
            headers.append(header);
            headerBlockNumbers.append(block.blockNumber());
            headerSequences.append(headerReg.cap(3));

            if (baseLevel == -1) {
                baseLevel = level;
            } else if (baseLevel > level) {
                baseLevel = level;
            }
        }
    }

    m_headers.clear();

    bool autoSequence = m_config.m_enableHeadingSequence
                        && !isReadOnly()
                        && m_file->isModifiable();
    int headingSequenceBaseLevel = g_config->getHeadingSequenceBaseLevel();
    if (headingSequenceBaseLevel < 1 || headingSequenceBaseLevel > 6) {
        headingSequenceBaseLevel = 1;
    }

    QVector<int> seqs(7, 0);
    QRegExp preReg(VUtils::c_headerPrefixRegExp);
    int curLevel = baseLevel - 1;
    QTextCursor cursor(doc);
    if(autoSequence || p_configChanged) {
        cursor.beginEditBlock();
    }

    for (int i = 0; i < headers.size(); ++i) {
        VTableOfContentItem &item = headers[i];
        while (item.m_level > curLevel + 1) {
            curLevel += 1;

            // Insert empty level which is an invalid header.
            m_headers.append(VTableOfContentItem(c_emptyHeaderName,
                                                 curLevel,
                                                 -1,
                                                 m_headers.size()));
            if (autoSequence || p_configChanged) {
                addHeaderSequence(seqs, curLevel, headingSequenceBaseLevel);
            }
        }

        item.m_index = m_headers.size();
        m_headers.append(item);
        curLevel = item.m_level;
        if (autoSequence || p_configChanged) {
            addHeaderSequence(seqs, item.m_level, headingSequenceBaseLevel);

            QString seqStr = autoSequence ? headerSequenceStr(seqs) : "";
            if (headerSequences[i] != seqStr) {
                // Insert correct sequence.
                insertSequenceToHeader(cursor,
                                       doc->findBlockByNumber(headerBlockNumbers[i]),
                                       headerReg,
                                       preReg,
                                       seqStr);
            }
        }
    }

    if (autoSequence || p_configChanged) {
        cursor.endEditBlock();
    }

    emit headersChanged(m_headers);

    updateCurrentHeader();
}

void VMdEditor::updateHeaders(const QVector<VElementRegion> &p_headerRegions)
{
    updateHeadersHelper(p_headerRegions, false);
}

void VMdEditor::updateCurrentHeader()
{
    emit currentHeaderChanged(textCursor().block().blockNumber());
}

void VMdEditor::initInitImages()
{
    m_initImages = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                       ImageLink::LocalRelativeInternal);
}

void VMdEditor::clearUnusedImages()
{
    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                                    ImageLink::LocalRelativeInternal);

    QSet<QString> unusedImages;

    if (!m_insertedImages.isEmpty()) {
        for (int i = 0; i < m_insertedImages.size(); ++i) {
            const ImageLink &link = m_insertedImages[i];

            if (link.m_type != ImageLink::LocalRelativeInternal) {
                continue;
            }

            int j;
            for (j = 0; j < images.size(); ++j) {
                if (VUtils::equalPath(link.m_path, images[j].m_path)) {
                    break;
                }
            }

            // This inserted image is no longer in the file.
            if (j == images.size()) {
                unusedImages.insert(link.m_path);
            }
        }

        m_insertedImages.clear();
    }

    for (int i = 0; i < m_initImages.size(); ++i) {
        const ImageLink &link = m_initImages[i];

        V_ASSERT(link.m_type == ImageLink::LocalRelativeInternal);

        int j;
        for (j = 0; j < images.size(); ++j) {
            if (VUtils::equalPath(link.m_path, images[j].m_path)) {
                break;
            }
        }

        // Original local relative image is no longer in the file.
        if (j == images.size()) {
            unusedImages.insert(link.m_path);
        }
    }

    if (!unusedImages.isEmpty()) {
        if (g_config->getConfirmImagesCleanUp()) {
            QVector<ConfirmItemInfo> items;
            for (auto const & img : unusedImages) {
                items.push_back(ConfirmItemInfo(img,
                                                img,
                                                img,
                                                NULL));

            }

            QString text = tr("Following images seems not to be used in this note anymore. "
                              "Please confirm the deletion of these images.");

            QString info = tr("Deleted files could be found in the recycle "
                              "bin of this note.<br>"
                              "Click \"Cancel\" to leave them untouched.");

            VConfirmDeletionDialog dialog(tr("Confirm Cleaning Up Unused Images"),
                                          text,
                                          info,
                                          items,
                                          true,
                                          true,
                                          true,
                                          this);

            unusedImages.clear();
            if (dialog.exec()) {
                items = dialog.getConfirmedItems();
                g_config->setConfirmImagesCleanUp(dialog.getAskAgainEnabled());

                for (auto const & item : items) {
                    unusedImages.insert(item.m_name);
                }
            }
        }

        for (auto const & item : unusedImages) {
            bool ret = false;
            if (m_file->getType() == FileType::Note) {
                const VNoteFile *tmpFile = static_cast<const VNoteFile *>((VFile *)m_file);
                ret = VUtils::deleteFile(tmpFile->getNotebook(), item, false);
            } else if (m_file->getType() == FileType::Orphan) {
                const VOrphanFile *tmpFile = static_cast<const VOrphanFile *>((VFile *)m_file);
                ret = VUtils::deleteFile(tmpFile, item, false);
            } else {
                Q_ASSERT(false);
            }

            if (!ret) {
                qWarning() << "fail to delete unused original image" << item;
            } else {
                qDebug() << "delete unused image" << item;
            }
        }
    }

    m_initImages.clear();
}

void VMdEditor::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    int modifiers = p_event->modifiers();
    switch (key) {
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        // Zoom out.
        if (modifiers & Qt::ControlModifier) {
            zoomPage(false);
            return;
        }

        break;

    case Qt::Key_Plus:
    case Qt::Key_Equal:
        // Zoom in.
        if (modifiers & Qt::ControlModifier) {
            zoomPage(true);
            return;
        }

        break;

    case Qt::Key_0:
        // Restore zoom.
        if (modifiers & Qt::ControlModifier) {
            if (m_zoomDelta > 0) {
                zoomPage(false, m_zoomDelta);
            } else if (m_zoomDelta < 0) {
                zoomPage(true, -m_zoomDelta);
            }

            return;
        }

        break;

    default:
        break;
    }

    if (m_editOps && m_editOps->handleKeyPressEvent(p_event)) {
        return;
    }

    // Esc to exit edit mode when Vim is disabled.
    if (key == Qt::Key_Escape) {
        emit m_object->discardAndRead();
        return;
    }

    VTextEdit::keyPressEvent(p_event);
}

bool VMdEditor::canInsertFromMimeData(const QMimeData *p_source) const
{
    return p_source->hasImage()
           || p_source->hasUrls()
           || VTextEdit::canInsertFromMimeData(p_source);
}

void VMdEditor::insertFromMimeData(const QMimeData *p_source)
{
    if (processHtmlFromMimeData(p_source)) {
        return;
    }

    if (processImageFromMimeData(p_source)) {
        return;
    }

    if (processUrlFromMimeData(p_source)) {
        return;
    }

    VTextEdit::insertFromMimeData(p_source);
}

void VMdEditor::imageInserted(const QString &p_path, const QString &p_url)
{
    ImageLink link;
    link.m_path = p_path;
    link.m_url = p_url;
    if (m_file->useRelativeImageFolder()) {
        link.m_type = ImageLink::LocalRelativeInternal;
    } else {
        link.m_type = ImageLink::LocalAbsolute;
    }

    m_insertedImages.append(link);
}

bool VMdEditor::scrollToHeader(int p_blockNumber)
{
    if (p_blockNumber < 0) {
        return false;
    }

    return scrollToBlock(p_blockNumber);
}

int VMdEditor::indexOfCurrentHeader() const
{
    if (m_headers.isEmpty()) {
        return -1;
    }

    int blockNumber = textCursor().block().blockNumber();
    for (int i = m_headers.size() - 1; i >= 0; --i) {
        if (!m_headers[i].isEmpty()
            && m_headers[i].m_blockNumber <= blockNumber) {
            return i;
        }
    }

    return -1;
}

bool VMdEditor::jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat)
{
    if (m_headers.isEmpty()) {
        return false;
    }

    QTextCursor cursor = textCursor();
    int cursorLine = cursor.block().blockNumber();
    int targetIdx = -1;
    // -1: skip level check.
    int targetLevel = 0;
    int idx = indexOfCurrentHeader();
    if (idx == -1) {
        // Cursor locates at the beginning, before any headers.
        if (p_relativeLevel < 0 || !p_forward) {
            return false;
        }
    }

    int delta = 1;
    if (!p_forward) {
        delta = -1;
    }

    bool firstHeader = true;
    for (targetIdx = idx == -1 ? 0 : idx;
         targetIdx >= 0 && targetIdx < m_headers.size();
         targetIdx += delta) {
        const VTableOfContentItem &header = m_headers[targetIdx];
        if (header.isEmpty()) {
            continue;
        }

        if (targetLevel == 0) {
            // The target level has not been init yet.
            Q_ASSERT(firstHeader);
            targetLevel = header.m_level;
            if (p_relativeLevel < 0) {
                targetLevel += p_relativeLevel;
                if (targetLevel < 1) {
                    // Invalid level.
                    return false;
                }
            } else if (p_relativeLevel > 0) {
                targetLevel = -1;
            }
        }

        if (targetLevel == -1 || header.m_level == targetLevel) {
            if (firstHeader
                && (cursorLine == header.m_blockNumber
                    || p_forward)
                && idx != -1) {
                // This header is not counted for the repeat.
                firstHeader = false;
                continue;
            }

            if (--p_repeat == 0) {
                // Found.
                break;
            }
        } else if (header.m_level < targetLevel) {
            // Stop by higher level.
            return false;
        }

        firstHeader = false;
    }

    if (targetIdx < 0 || targetIdx >= m_headers.size()) {
        return false;
    }

    // Jump to target header.
    int line = m_headers[targetIdx].m_blockNumber;
    if (line > -1) {
        QTextBlock block = document()->findBlockByNumber(line);
        if (block.isValid()) {
            cursor.setPosition(block.position());
            setTextCursor(cursor);
            return true;
        }
    }

    return false;
}

void VMdEditor::scrollBlockInPage(int p_blockNum, int p_dest, int p_margin)
{
    VEditUtils::scrollBlockInPage(this, p_blockNum, p_dest, p_margin);
}

void VMdEditor::updateTextEditConfig()
{
    setBlockImageEnabled(g_config->getEnablePreviewImages());

    setImageWidthConstrainted(g_config->getEnablePreviewImageConstraint());

    setLineLeading(m_config.m_lineDistanceHeight);

    setImageLineColor(g_config->getEditorPreviewImageLineFg());

    setEnableExtraBuffer(g_config->getEnableExtraBuffer());

    int lineNumber = g_config->getEditorLineNumber();
    if (lineNumber < (int)LineNumberType::None || lineNumber >= (int)LineNumberType::Invalid) {
        lineNumber = (int)LineNumberType::None;
    }

    setLineNumberType((LineNumberType)lineNumber);
    setLineNumberColor(g_config->getEditorLineNumberFg(),
                       g_config->getEditorLineNumberBg());

    m_previewMgr->setPreviewEnabled(g_config->getEnablePreviewImages());
}

void VMdEditor::updateConfig()
{
    updateEditConfig();
    updateTextEditConfig();
}

QString VMdEditor::getContent() const
{
    return toPlainText();
}

void VMdEditor::setContent(const QString &p_content, bool p_modified)
{
    if (p_modified) {
        QTextCursor cursor = textCursor();
        cursor.select(QTextCursor::Document);
        cursor.insertText(p_content);
        setTextCursor(cursor);
    } else {
        setPlainText(p_content);
    }
}

void VMdEditor::refreshPreview()
{
    m_previewMgr->refreshPreview();
}

void VMdEditor::updateInitAndInsertedImages(bool p_fileChanged, UpdateAction p_act)
{
    if (p_fileChanged && p_act == UpdateAction::InfoChanged) {
        return;
    }

    if (!isModified()) {
        Q_ASSERT(m_insertedImages.isEmpty());
        m_insertedImages.clear();

        if (!m_initImages.isEmpty()) {
            // Re-generate init images.
            initInitImages();
        }

        return;
    }

    // Update init images.
    QVector<ImageLink> tmp = m_initImages;
    initInitImages();
    Q_ASSERT(tmp.size() == m_initImages.size());

    QDir dir(m_file->fetchBasePath());

    // File has been moved.
    if (p_fileChanged) {
        // Since we clear unused images once user save the note, all images
        // in m_initImages now are moved already.

        // Update inserted images.
        // Inserted images should be moved manually here. Then update all the
        // paths.
        for (auto & link : m_insertedImages) {
            if (link.m_type == ImageLink::LocalAbsolute) {
                continue;
            }

            QString newPath = QDir::cleanPath(dir.absoluteFilePath(link.m_url));
            if (VUtils::equalPath(link.m_path, newPath)) {
                continue;
            }

            if (!VUtils::copyFile(link.m_path, newPath, true)) {
                VUtils::showMessage(QMessageBox::Warning,
                                    tr("Warning"),
                                    tr("Fail to move unsaved inserted image %1 to %2.")
                                    .arg(link.m_path)
                                    .arg(newPath),
                                    tr("Please check it manually to avoid image loss."),
                                    QMessageBox::Ok,
                                    QMessageBox::Ok,
                                    this);
                continue;
            }

            link.m_path = newPath;
        }
    } else {
        // Directory changed.
        // Update inserted images.
        for (auto & link : m_insertedImages) {
            if (link.m_type == ImageLink::LocalAbsolute) {
                continue;
            }

            QString newPath = QDir::cleanPath(dir.absoluteFilePath(link.m_url));
            link.m_path = newPath;
        }
    }
}

void VMdEditor::handleCopyAsAction(QAction *p_act)
{
    ++m_copyTimeStamp;

    QTextCursor cursor = textCursor();
    Q_ASSERT(cursor.hasSelection());

    QString text = VEditUtils::selectedText(cursor);
    Q_ASSERT(!text.isEmpty());

    Q_ASSERT(!m_textToHtmlDialog);
    m_textToHtmlDialog = new VCopyTextAsHtmlDialog(text, p_act->data().toString(), this);

    // For Hoedown, we use marked.js to convert the text to have a general interface.
    emit requestTextToHtml(text, 0, m_copyTimeStamp);

    m_textToHtmlDialog->exec();

    delete m_textToHtmlDialog;
    m_textToHtmlDialog = NULL;
}

void VMdEditor::textToHtmlFinished(int p_id,
                                   int p_timeStamp,
                                   const QUrl &p_baseUrl,
                                   const QString &p_html)
{
    Q_UNUSED(p_id);
    if (m_textToHtmlDialog && p_timeStamp == m_copyTimeStamp) {
        m_textToHtmlDialog->setConvertedHtml(p_baseUrl, p_html);
    }
}

void VMdEditor::htmlToTextFinished(int p_id, int p_timeStamp, const QString &p_text)
{
    Q_UNUSED(p_id);
    if (m_copyTimeStamp == p_timeStamp && !p_text.isEmpty()) {
        emit m_object->statusMessage(tr("Inserting parsed Markdown text"));

        QString text(p_text);
        if (g_config->getParsePasteLocalImage()) {
            // May take long time.
            replaceTextWithLocalImages(text);
        }

        m_editOps->insertText(text);
        emit m_object->statusMessage(tr("Parsed Markdown text inserted"));
    }
}

void VMdEditor::wheelEvent(QWheelEvent *p_event)
{
    if (handleWheelEvent(p_event)) {
        return;
    }

    VTextEdit::wheelEvent(p_event);
}

void VMdEditor::zoomPage(bool p_zoomIn, int p_range)
{
    if (p_range == 0) {
        return;
    }

    const int minSize = 2;

    int delta = p_zoomIn ? p_range : -p_range;

    // zoomIn() and zoomOut() does not work if we set stylesheet of VMdEditor.
    int ptSz = font().pointSize() + delta;
    if (ptSz < minSize) {
        ptSz = minSize;
    }

    setFontPointSizeByStyleSheet(ptSz);

    emit m_object->statusMessage(QObject::tr("Set base font point size %1").arg(ptSz));

    m_zoomDelta += delta;

    QVector<HighlightingStyle> &styles = m_pegHighlighter->getStyles();
    for (auto & it : styles) {
        int size = it.format.fontPointSize();
        if (size == 0) {
            // It contains no font size format.
            continue;
        }

        size += delta;
        if (size < minSize) {
            size = minSize;
        }

        it.format.setFontPointSize(size);
    }

    QHash<QString, QTextCharFormat> &cbStyles = m_pegHighlighter->getCodeBlockStyles();
    for (auto it = cbStyles.begin(); it != cbStyles.end(); ++it) {
        int size = it.value().fontPointSize();
        if (size == 0) {
            // It contains no font size format.
            continue;
        }

        size += delta;
        if (size < minSize) {
            size = minSize;
        }

        it.value().setFontPointSize(size);
    }

    m_pegHighlighter->rehighlight();
}

QAction *VMdEditor::initCopyAsMenu(QAction *p_after, QMenu *p_menu)
{
    QStringList targets = g_webUtils->getCopyTargetsName();
    if (targets.isEmpty()) {
        return NULL;
    }

    QMenu *subMenu = new QMenu(tr("Copy HTML As"), p_menu);
    subMenu->setToolTipsVisible(true);
    for (auto const & target : targets) {
        QAction *act = new QAction(target, subMenu);
        act->setData(target);
        act->setToolTip(tr("Copy selected content as HTML using rules specified by target %1").arg(target));

        subMenu->addAction(act);
    }

    connect(subMenu, &QMenu::triggered,
            this, &VMdEditor::handleCopyAsAction);
    return insertMenuAfter(p_after, subMenu, p_menu);
}

QAction *VMdEditor::initPasteAsBlockQuoteMenu(QAction *p_after, QMenu *p_menu)
{
    QAction *pbqAct = new QAction(tr("Paste As Block &Quote"), p_menu);
    pbqAct->setToolTip(tr("Paste text from clipboard as block quote"));
    connect(pbqAct, &QAction::triggered,
            this, [this]() {
                QClipboard *clipboard = QApplication::clipboard();
                const QMimeData *mimeData = clipboard->mimeData();
                QString text = mimeData->text();

                QTextCursor cursor = textCursor();
                cursor.removeSelectedText();
                QTextBlock block = cursor.block();
                QString indent = VEditUtils::fetchIndentSpaces(block);

                // Insert '> ' in front of each line.
                VEditUtils::insertBeforeEachLine(text, indent + QStringLiteral("> "));

                if (VEditUtils::isSpaceBlock(block)) {
                    if (!indent.isEmpty()) {
                        // Remove the indent.
                        cursor.movePosition(QTextCursor::StartOfBlock);
                        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                        cursor.removeSelectedText();
                    }
                } else {
                    // Insert a new block.
                    VEditUtils::insertBlock(cursor, false);
                }

                cursor.insertText(text);
                setTextCursor(cursor);
            });


    insertActionAfter(p_after, pbqAct, p_menu);
    return pbqAct;
}

QAction *VMdEditor::initPasteAfterParseMenu(QAction *p_after, QMenu *p_menu)
{
    QAction *papAct = new QAction(tr("Paste Parsed &Markdown Text"), p_menu);
    VUtils::fixTextWithCaptainShortcut(papAct, "ParseAndPaste");
    papAct->setToolTip(tr("Parse HTML to Markdown text and paste"));
    connect(papAct, &QAction::triggered,
            this, &VMdEditor::parseAndPaste);

    insertActionAfter(p_after, papAct, p_menu);
    return papAct;
}

void VMdEditor::insertImageLink(const QString &p_text, const QString &p_url)
{
    VInsertLinkDialog dialog(tr("Insert Image Link"),
                             "",
                             "",
                             p_text,
                             p_url,
                             true,
                             this);
    if (dialog.exec() == QDialog::Accepted) {
        QString linkText = dialog.getLinkText();
        QString linkUrl = dialog.getLinkUrl();
        static_cast<VMdEditOperations *>(m_editOps)->insertImageLink(linkText, linkUrl);
    }
}

VWordCountInfo VMdEditor::fetchWordCountInfo() const
{
    VWordCountInfo info;
    QTextDocument *doc = document();

    // Char without spaces.
    int cns = 0;
    int wc = 0;
    // Remove th ending new line.
    int cc = doc->characterCount() - 1;
    // 0 - not in word;
    // 1 - in English word;
    // 2 - in non-English word;
    int state = 0;

    for (int i = 0; i < cc; ++i) {
        QChar ch = doc->characterAt(i);
        if (ch.isSpace()) {
            if (state) {
                state = 0;
            }

            continue;
        } else if (ch.unicode() < 128) {
            if (state != 1) {
                state = 1;
                ++wc;
            }
        } else {
            state = 2;
            ++wc;
        }

        ++cns;
    }

    info.m_mode = VWordCountInfo::Edit;
    info.m_wordCount = wc;
    info.m_charWithoutSpacesCount = cns;
    info.m_charWithSpacesCount = cc;
    return info;
}

void VMdEditor::setEditTab(VEditTab *p_editTab)
{
    m_editTab = p_editTab;
}

void VMdEditor::setFontPointSizeByStyleSheet(int p_ptSize)
{
    QFont ft = font();
    ft.setPointSize(p_ptSize);

    const QPalette &palette = g_config->getMdEditPalette();
    setFontAndPaletteByStyleSheet(ft, palette);

    ensurePolished();

    ft.setPointSize(p_ptSize + LINE_NUMBER_AREA_FONT_DELTA);
    updateLineNumberAreaWidth(QFontMetrics(ft));
}

void VMdEditor::setFontAndPaletteByStyleSheet(const QFont &p_font, const QPalette &p_palette)
{
    QString styles(QString("VMdEditor, VLineNumberArea {"
                           "font-family: \"%1\";"
                           "font-size: %2pt;"
                           "color: %3;"
                           "background-color: %4;"
                           "selection-color: %5;"
                           "selection-background-color: %6; } "
                           "VLineNumberArea {"
                           "font-size: %7pt; }")
                          .arg(p_font.family())
                          .arg(p_font.pointSize())
                          .arg(p_palette.color(QPalette::Text).name())
                          .arg(p_palette.color(QPalette::Base).name())
                          .arg(p_palette.color(QPalette::HighlightedText).name())
                          .arg(p_palette.color(QPalette::Highlight).name())
                          .arg(p_font.pointSize() + LINE_NUMBER_AREA_FONT_DELTA));

    setStyleSheet(styles);
}

int VMdEditor::lineNumberAreaWidth() const
{
    return VTextEdit::lineNumberAreaWidth();
}

void VMdEditor::initLinkAndPreviewMenu(QAction *p_before, QMenu *p_menu, const QPoint &p_pos)
{
    QTextCursor cursor = cursorForPosition(p_pos);
    const int pos = cursor.position();
    QTextBlock block = cursor.block();
    const QString text(block.text());

    // Image.
    QRegExp regExp(VUtils::c_imageLinkRegExp);
    if (regExp.indexIn(text) > -1) {
        const QVector<VElementRegion> &imgRegs = m_pegHighlighter->getImageRegions();
        for (auto const & reg : imgRegs) {
            if (!reg.contains(pos)
                && (!reg.contains(pos - 1) || pos != (block.position() + text.size()))) {
                continue;
            }

            if (reg.m_endPos > block.position() + text.length()) {
                return;
            }

            QString linkText = text.mid(reg.m_startPos - block.position(),
                                        reg.m_endPos - reg.m_startPos);
            QString surl = VUtils::fetchImageLinkUrl(linkText);
            if (surl.isEmpty()) {
                return;
            }

            QString imgPath = VUtils::linkUrlToPath(m_file->fetchBasePath(), surl);
            bool isLocalFile = QFileInfo::exists(imgPath);

            QAction *viewImageAct = new QAction(tr("View Image"), p_menu);
            connect(viewImageAct, &QAction::triggered,
                    this, [imgPath]() {
                        QDesktopServices::openUrl(VUtils::pathToUrl(imgPath));
                    });
            p_menu->insertAction(p_before, viewImageAct);

            QAction *copyImageLinkAct = new QAction(tr("Copy Image URL"), p_menu);
            connect(copyImageLinkAct, &QAction::triggered,
                    this, [imgPath]() {
                        QClipboard *clipboard = QApplication::clipboard();
                        VClipboardUtils::setLinkToClipboard(clipboard,
                                                            imgPath,
                                                            QClipboard::Clipboard);
                    });
            p_menu->insertAction(p_before, copyImageLinkAct);

            if (isLocalFile) {
                QAction *copyImagePathAct = new QAction(tr("Copy Image Path"), p_menu);
                connect(copyImagePathAct, &QAction::triggered,
                        this, [imgPath]() {
                            QClipboard *clipboard = QApplication::clipboard();
                            QMimeData *data = new QMimeData();
                            data->setText(imgPath);
                            VClipboardUtils::setMimeDataToClipboard(clipboard,
                                                                    data,
                                                                    QClipboard::Clipboard);
                        });
                p_menu->insertAction(p_before, copyImagePathAct);

                QAction *copyImageAct = new QAction(tr("Copy Image"), p_menu);
                connect(copyImageAct, &QAction::triggered,
                        this, [this, imgPath]() {
                            QClipboard *clipboard = QApplication::clipboard();
                            clipboard->clear();
                            QImage img = VUtils::imageFromFile(imgPath);
                            if (!img.isNull()) {
                                VClipboardUtils::setImageToClipboard(clipboard,
                                                                     img,
                                                                     QClipboard::Clipboard);
                            }
                        });
                p_menu->insertAction(p_before, copyImageAct);
            } else {
                // Copy in-place preview.
                initInPlacePreviewMenu(p_before, p_menu, block, pos);
            }

            p_menu->insertSeparator(p_before);
            return;
        }
    }

    // Link.
    QRegExp regExp2(VUtils::c_linkRegExp);
    QString linkText;
    int p = 0;
    const int pib = pos - block.position();
    while (p < text.size()) {
        int idx = text.indexOf(regExp2, p);
        if (idx == -1) {
            break;
        }

        p = idx + regExp2.matchedLength();
        if (pib >= idx && pib < p) {
            linkText = regExp2.cap(2);
            break;
        }
    }

    if (!linkText.isEmpty()) {
        QString linkUrl = VUtils::linkUrlToPath(m_file->fetchBasePath(), linkText);
        bool isLocalFile = QFileInfo::exists(linkUrl);

        QAction *viewLinkAct = new QAction(tr("View Link"), p_menu);
        connect(viewLinkAct, &QAction::triggered,
                this, [linkUrl]() {
                    QDesktopServices::openUrl(VUtils::pathToUrl(linkUrl));
                });
        p_menu->insertAction(p_before, viewLinkAct);

        QAction *copyLinkAct = new QAction(tr("Copy Link URL"), p_menu);
        connect(copyLinkAct, &QAction::triggered,
                this, [linkUrl]() {
                    QClipboard *clipboard = QApplication::clipboard();
                    VClipboardUtils::setLinkToClipboard(clipboard,
                                                        linkUrl,
                                                        QClipboard::Clipboard);
                });
        p_menu->insertAction(p_before, copyLinkAct);

        if (isLocalFile) {
            QAction *copyLinkPathAct = new QAction(tr("Copy Link Path"), p_menu);
            connect(copyLinkPathAct, &QAction::triggered,
                    this, [linkUrl]() {
                        QClipboard *clipboard = QApplication::clipboard();
                        QMimeData *data = new QMimeData();
                        data->setText(linkUrl);
                        VClipboardUtils::setMimeDataToClipboard(clipboard,
                                                                data,
                                                                QClipboard::Clipboard);
                    });
            p_menu->insertAction(p_before, copyLinkPathAct);
        }

        p_menu->insertSeparator(p_before);
        return;
    }

    bool needSeparator = false;
    if (initInPlacePreviewMenu(p_before, p_menu, block, pos)) {
        needSeparator = true;
    }

    if (initExportAndCopyMenu(p_before, p_menu, block, pos)) {
        needSeparator = true;
    }

    if (needSeparator) {
        p_menu->insertSeparator(p_before);
    }
}

bool VMdEditor::initInPlacePreviewMenu(QAction *p_before,
                                       QMenu *p_menu,
                                       const QTextBlock &p_block,
                                       int p_pos)
{
    VTextBlockData *data = VTextBlockData::blockData(p_block);
    if (!data) {
        return false;
    }

    const QVector<VPreviewInfo *> &previews = data->getPreviews();
    if (previews.isEmpty()) {
        return false;
    }

    QPixmap image;
    QString background;
    int pib = p_pos - p_block.position();
    for (auto info : previews) {
        const VPreviewedImageInfo &pii = info->m_imageInfo;
        if (pii.contains(pib)
            || (pii.contains(pib - 1) && pib == p_block.length() - 1)) {
            const QPixmap *img = findImage(pii.m_imageName);
            if (img) {
                image = *img;
                background = pii.m_background;
            }

            break;
        }
    }

    if (image.isNull()) {
        return false;
    }

    QAction *copyImageAct = new QAction(tr("Copy In-Place Preview"), p_menu);
    connect(copyImageAct, &QAction::triggered,
            this, [this, image, background]() {
                QColor co(background);
                if (!co.isValid()) {
                    co = palette().color(QPalette::Base);
                }

                QImage img(image.size(), QImage::Format_ARGB32);
                img.fill(co);
                QPainter pter(&img);
                pter.drawPixmap(img.rect(), image);

                QClipboard *clipboard = QApplication::clipboard();
                VClipboardUtils::setImageToClipboard(clipboard,
                                                     img,
                                                     QClipboard::Clipboard);
            });
    p_menu->insertAction(p_before, copyImageAct);
    return true;
}

bool VMdEditor::initExportAndCopyMenu(QAction *p_before,
                                      QMenu *p_menu,
                                      const QTextBlock &p_block,
                                      int p_pos)
{
    Q_UNUSED(p_pos);
    int state = p_block.userState();
    if (state != HighlightBlockState::CodeBlockStart
        && state != HighlightBlockState::CodeBlock
        && state != HighlightBlockState::CodeBlockEnd) {
        return false;
    }

    int blockNum = p_block.blockNumber();
    const QVector<VCodeBlock> &cbs = m_pegHighlighter->getCodeBlocks();
    int idx = 0;
    for (idx = 0; idx < cbs.size(); ++idx) {
        if (cbs[idx].m_startBlock <= blockNum
            && cbs[idx].m_endBlock >= blockNum) {
            break;
        }
    }

    if (idx >= cbs.size()) {
        return false;
    }

    const VCodeBlock &cb = cbs[idx];
    if (cb.m_lang != "puml" && cb.m_lang != "dot") {
        return false;
    }

    QMenu *subMenu = new QMenu(tr("Copy Graph"), p_menu);
    subMenu->setToolTipsVisible(true);

    QAction *pngAct = new QAction(tr("PNG"), subMenu);
    pngAct->setToolTip(tr("Export graph as PNG to a temporary file and copy"));
    connect(pngAct, &QAction::triggered,
            this, [this, lang = cb.m_lang, text = cb.m_text]() {
                exportGraphAndCopy(lang, text, "png");
            });
    subMenu->addAction(pngAct);

    QAction *svgAct = new QAction(tr("SVG"), subMenu);
    svgAct->setToolTip(tr("Export graph as SVG to a temporary file and copy"));
    connect(svgAct, &QAction::triggered,
            this, [this, lang = cb.m_lang, text = cb.m_text]() {
                exportGraphAndCopy(lang, text, "svg");
            });
    subMenu->addAction(svgAct);

    p_menu->insertMenu(p_before, subMenu);
    return true;
}

void VMdEditor::exportGraphAndCopy(const QString &p_lang,
                                   const QString &p_text,
                                   const QString &p_format)
{
    m_exportTempFile.reset(VUtils::createTemporaryFile(p_format));
    if (!m_exportTempFile->open()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to open a temporary file for export."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        m_exportTempFile.clear();
        return;
    }

    emit m_object->statusMessage(tr("Exporting graph"));

    QString filePath(m_exportTempFile->fileName());
    QByteArray out;
    if (p_lang == "puml") {
        out = VPlantUMLHelper::process(p_format,
                                       VEditUtils::removeCodeBlockFence(p_text));
    } else if (p_lang == "dot") {
        out = VGraphvizHelper::process(p_format,
                                       VEditUtils::removeCodeBlockFence(p_text));
    }

    if (out.isEmpty() || m_exportTempFile->write(out) == -1) {
        m_exportTempFile->close();
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to export graph."),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    } else {
        m_exportTempFile->close();

        QClipboard *clipboard = QApplication::clipboard();
        clipboard->clear();

        QImage img;
        img.loadFromData(out, p_format.toLocal8Bit().data());
        if (!img.isNull()) {
            VClipboardUtils::setImageAndLinkToClipboard(clipboard,
                                                        img,
                                                        filePath,
                                                        QClipboard::Clipboard);
            emit m_object->statusMessage(tr("Graph exported and copied"));
        } else {
            emit m_object->statusMessage(tr("Fail to read exported image: %1").arg(filePath));
        }
    }
}

void VMdEditor::parseAndPaste()
{
    if (!m_editTab
        || !m_editTab->isEditMode()
        || isReadOnly()) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    QString html(mimeData->html());
    if (!html.isEmpty()) {
        ++m_copyTimeStamp;
        emit requestHtmlToText(html, 0, m_copyTimeStamp);
    }
}

bool VMdEditor::processHtmlFromMimeData(const QMimeData *p_source)
{
    if (!p_source->hasHtml()) {
        return false;
    }

    // Handle <img>.
    QRegExp reg("<img ([^>]*)src=\"([^\"]+)\"([^>]*)>");
    QString html(p_source->html());
    if (reg.indexIn(html) != -1 && VUtils::onlyHasImgInHtml(html)) {
        if (p_source->hasImage()) {
            // Both image data and URL are embedded.
            VSelectDialog dialog(tr("Insert From Clipboard"), this);
            dialog.addSelection(tr("Insert From URL"), 0);
            dialog.addSelection(tr("Insert From Image Data"), 1);
            dialog.addSelection(tr("Insert As Image Link"), 2);

            if (dialog.exec() == QDialog::Accepted) {
                int selection = dialog.getSelection();
                if (selection == 1) {
                    // Insert from image data.
                    m_editOps->insertImageFromMimeData(p_source);
                    return true;
                } else if (selection == 2) {
                    // Insert as link.
                    insertImageLink("", reg.cap(2));
                    return true;
                }
            } else {
                return true;
            }
        }

        m_editOps->insertImageFromURL(QUrl(reg.cap(2)));
        return true;
    }

    return false;
}

bool VMdEditor::processImageFromMimeData(const QMimeData *p_source)
{
    if (!p_source->hasImage()) {
        return false;
    }

    // Image data in the clipboard
    if (p_source->hasText()) {
        VSelectDialog dialog(tr("Insert From Clipboard"), this);
        dialog.addSelection(tr("Insert As Image"), 0);
        dialog.addSelection(tr("Insert As Text"), 1);
        dialog.addSelection(tr("Insert As Image Link"), 2);

        if (dialog.exec() == QDialog::Accepted) {
            int selection = dialog.getSelection();
            if (selection == 1) {
                // Insert as text.
                Q_ASSERT(p_source->hasText() && p_source->hasImage());
                VTextEdit::insertFromMimeData(p_source);
                return true;
            } else if (selection == 2) {
                // Insert as link.
                insertImageLink("", p_source->text());
                return true;
            }
        } else {
            return true;
        }
    }

    m_editOps->insertImageFromMimeData(p_source);
    return true;
}

bool VMdEditor::processUrlFromMimeData(const QMimeData *p_source)
{
    QUrl url;
    if (p_source->hasUrls()) {
        QList<QUrl> urls = p_source->urls();
        if (urls.size() == 1) {
            url = urls[0];
        }
    } else if (p_source->hasText()) {
        // Try to get URL from text.
        QString text = p_source->text();
        if (QFileInfo::exists(text)) {
            url = QUrl::fromLocalFile(text);
        } else {
            url = QUrl(text);
            if (url.scheme() != "https" && url.scheme() != "http") {
                url.clear();
            }
        }
    }

    if (!url.isValid()) {
        return false;
    }

    bool isImage = VUtils::isImageURL(url);
    bool isLocalFile = url.isLocalFile()
                       && QFileInfo::exists(url.toLocalFile());

    QString localTextFilePath;
    if (!isImage && isLocalFile) {
        localTextFilePath = url.toLocalFile();
        QMimeDatabase mimeDatabase;
        const QMimeType mimeType = mimeDatabase.mimeTypeForFile(localTextFilePath);
        if (mimeType.isValid() && !mimeType.inherits(QStringLiteral("text/plain"))) {
            localTextFilePath.clear();
        }
    }

    VSelectDialog dialog(tr("Insert From Clipboard"), this);
    if (isImage) {
        dialog.addSelection(tr("Insert As Image"), 0);
        dialog.addSelection(tr("Insert As Image Link"), 1);
        if (isLocalFile) {
            dialog.addSelection(tr("Insert As Relative Image Link"), 7);
        }
    }

    dialog.addSelection(tr("Insert As Link"), 2);
    if (isLocalFile) {
        dialog.addSelection(tr("Insert As Relative Link"), 3);

        // Attach as attachment.
        if (m_file->getType() == FileType::Note) {
            VNoteFile *note = static_cast<VNoteFile *>((VFile *)m_file);
            if (-1 == note->findAttachmentByPath(url.toLocalFile(), false)) {
                dialog.addSelection(tr("Attach And Insert Link"), 6);
            }
        }
    }

    dialog.addSelection(tr("Insert As Text"), 4);
    if (!localTextFilePath.isEmpty()) {
        dialog.addSelection(tr("Insert File Content"), 5);
    }

    // FIXME: After calling dialog.exec(), p_source->hasUrl() returns false.
    if (dialog.exec() == QDialog::Accepted) {
        bool relativeLink = false;
        switch (dialog.getSelection()) {
        case 0:
        {
            // Insert As Image.
            m_editOps->insertImageFromURL(url);
            return true;
        }

        case 7:
            // Insert As Relative Image Link.
            relativeLink = true;
            V_FALLTHROUGH;

        case 1:
        {
            // Insert As Image Link.
            QString ut;
            if (relativeLink) {
                QDir dir(m_file->fetchBasePath());
                ut = dir.relativeFilePath(url.toLocalFile());
                ut = QUrl(ut).toString(QUrl::EncodeSpaces);
                if (g_config->getPrependDotInRelativePath()) {
                    VUtils::prependDotIfRelative(ut);
                }
            } else {
                ut = url.toString(QUrl::EncodeSpaces);
            }

            insertImageLink("", ut);
            return true;
        }

        case 6:
        {
            // Attach And Insert Link.
            QString file = url.toLocalFile();

            Q_ASSERT(m_file->getType() == FileType::Note);
            VNoteFile *note = static_cast<VNoteFile *>((VFile *)m_file);
            QString destFile;
            if (!note->addAttachment(file, &destFile)) {
                VUtils::showMessage(QMessageBox::Warning,
                                    tr("Warning"),
                                    tr("Fail to add attachment %1 for note <span style=\"%2\">%3</span>.")
                                      .arg(file)
                                      .arg(g_config->c_dataTextStyle)
                                      .arg(note->getName()),
                                    "",
                                    QMessageBox::Ok,
                                    QMessageBox::Ok,
                                    this);
                return true;
            }

            emit m_object->statusMessage(tr("1 file added as attachment"));

            // Update url to point to the attachment file.
            Q_ASSERT(!destFile.isEmpty());
            url = QUrl::fromLocalFile(destFile);

            V_FALLTHROUGH;
        }

        case 3:
            // Insert As Relative link.
            relativeLink = true;
            V_FALLTHROUGH;

        case 2:
        {
            // Insert As Link.
            QString initLinkText;
            if (isLocalFile) {
                initLinkText = QFileInfo(url.toLocalFile()).fileName();
            }

            QString ut;
            if (relativeLink) {
                QDir dir(m_file->fetchBasePath());
                ut = dir.relativeFilePath(url.toLocalFile());
                ut = QUrl(ut).toString(QUrl::EncodeSpaces);
                if (g_config->getPrependDotInRelativePath()) {
                    VUtils::prependDotIfRelative(ut);
                }
            } else {
                ut = url.toString(QUrl::EncodeSpaces);
            }

            VInsertLinkDialog ld(QObject::tr("Insert Link"),
                                 "",
                                 "",
                                 initLinkText,
                                 ut,
                                 false,
                                 this);
            if (ld.exec() == QDialog::Accepted) {
                QString linkText = ld.getLinkText();
                QString linkUrl = ld.getLinkUrl();
                Q_ASSERT(!linkText.isEmpty() && !linkUrl.isEmpty());
                m_editOps->insertLink(linkText, linkUrl);
            }

            return true;
        }

        case 4:
        {
            // Insert As Text.
            if (p_source->hasText()) {
                m_editOps->insertText(p_source->text());
            } else {
                m_editOps->insertText(url.toString());
            }

            return true;
        }

        case 5:
        {
            // Insert File Content.
            Q_ASSERT(!localTextFilePath.isEmpty());
            m_editOps->insertText(VUtils::readFileFromDisk(localTextFilePath));
            return true;
        }

        default:
            Q_ASSERT(false);
            break;
        }
    }

    return true;
}

void VMdEditor::replaceTextWithLocalImages(QString &p_text)
{
    QVector<VElementRegion> regs = VUtils::fetchImageRegionsUsingParser(p_text);
    if (regs.isEmpty()) {
        return;
    }

    // Sort it in ascending order.
    std::sort(regs.begin(), regs.end());

    QProgressDialog proDlg(tr("Fetching images to local folder..."),
                           tr("Abort"),
                           0,
                           regs.size(),
                           this);
    proDlg.setWindowModality(Qt::WindowModal);
    proDlg.setWindowTitle(tr("Fetching Images To Local Folder"));

    QRegExp zhihuRegExp("^https?://www\\.zhihu\\.com/equation\\?tex=(.+)$");

    QRegExp regExp(VUtils::c_imageLinkRegExp);
    for (int i = regs.size() - 1; i >= 0; --i) {
        proDlg.setValue(regs.size() - 1 - i);
        if (proDlg.wasCanceled()) {
            break;
        }

        const VElementRegion &reg = regs[i];
        QString linkText = p_text.mid(reg.m_startPos, reg.m_endPos - reg.m_startPos);
        if (regExp.indexIn(linkText) == -1) {
            continue;
        }

        QString imageTitle = VUtils::purifyImageTitle(regExp.cap(1).trimmed());
        QString imageUrl = regExp.cap(2).trimmed();

        const int maxUrlLength = 100;
        QString urlToDisplay(imageUrl);
        if (urlToDisplay.size() > maxUrlLength) {
            urlToDisplay = urlToDisplay.left(maxUrlLength) + "...";
        }
        proDlg.setLabelText(tr("Fetching image: %1").arg(urlToDisplay));

        // Handle equation from zhihu.com like http://www.zhihu.com/equation?tex=P.
        if (zhihuRegExp.indexIn(imageUrl) != -1) {
            QString tex = zhihuRegExp.cap(1).trimmed();

            // Remove the +.
            tex.replace(QChar('+'), " ");

            tex = QUrl::fromPercentEncoding(tex.toUtf8());
            if (tex.isEmpty()) {
                continue;
            }

            tex = "$" + tex + "$";
            p_text.replace(reg.m_startPos,
                           reg.m_endPos - reg.m_startPos,
                           tex);
            continue;
        }

        QString destImagePath, urlInLink;

        // Only handle absolute file path or network path.
        QString srcImagePath;
        QFileInfo info(VUtils::purifyUrl(imageUrl));

        // For network image.
        QScopedPointer<QTemporaryFile> tmpFile;

        if (info.exists()) {
            if (info.isAbsolute()) {
                // Absolute local path.
                srcImagePath = info.absoluteFilePath();
            }
        } else {
            // Network path.
            QByteArray data = VDownloader::downloadSync(QUrl(imageUrl));
            if (!data.isEmpty()) {
                tmpFile.reset(VUtils::createTemporaryFile(info.suffix()));
                if (tmpFile->open() && tmpFile->write(data) > -1) {
                    srcImagePath = tmpFile->fileName();
                }

                // Need to close it explicitly to flush cache of small file.
                tmpFile->close();
            }
        }

        if (srcImagePath.isEmpty()) {
            continue;
        }

        // Insert image without inserting text.
        auto ops = static_cast<VMdEditOperations *>(m_editOps);
        ops->insertImageFromPath(imageTitle,
                                 m_file->fetchImageFolderPath(),
                                 m_file->getImageFolderInLink(),
                                 srcImagePath,
                                 false,
                                 destImagePath,
                                 urlInLink);
        if (urlInLink.isEmpty()) {
            continue;
        }

        // Replace URL in link.
        QString newLink = QString("![%1](%2%3%4)")
                                 .arg(imageTitle)
                                 .arg(urlInLink)
                                 .arg(regExp.cap(3))
                                 .arg(regExp.cap(6));
        p_text.replace(reg.m_startPos,
                       reg.m_endPos - reg.m_startPos,
                       newLink);
    }

    proDlg.setValue(regs.size());
}

void VMdEditor::initAttachmentMenu(QMenu *p_menu)
{
    if (m_file->getType() != FileType::Note) {
        return;
    }

    const VNoteFile *note = static_cast<const VNoteFile *>((VFile *)m_file);
    const QVector<VAttachment> &attas = note->getAttachments();
    if (attas.isEmpty()) {
        return;
    }

    QMenu *subMenu = new QMenu(tr("Link To Attachment"), p_menu);
    for (auto const & att : attas) {
        QAction *act = new QAction(att.m_name, subMenu);
        act->setData(att.m_name);
        subMenu->addAction(act);
    }

    connect(subMenu, &QMenu::triggered,
            this, &VMdEditor::handleLinkToAttachmentAction);

    p_menu->addSeparator();
    p_menu->addMenu(subMenu);
}

void VMdEditor::handleLinkToAttachmentAction(QAction *p_act)
{
    Q_ASSERT(m_file->getType() == FileType::Note);
    VNoteFile *note = static_cast<VNoteFile *>((VFile *)m_file);
    QString name = p_act->data().toString();
    QString folderPath = note->fetchAttachmentFolderPath();
    QString filePath = QDir(folderPath).filePath(name);

    QDir dir(note->fetchBasePath());
    QString ut = dir.relativeFilePath(filePath);
    ut = QUrl(ut).toString(QUrl::EncodeSpaces);
    if (g_config->getPrependDotInRelativePath()) {
        VUtils::prependDotIfRelative(ut);
    }

    VInsertLinkDialog ld(QObject::tr("Insert Link"),
                         "",
                         "",
                         name,
                         ut,
                         false,
                         this);
    if (ld.exec() == QDialog::Accepted) {
        QString linkText = ld.getLinkText();
        QString linkUrl = ld.getLinkUrl();
        Q_ASSERT(!linkText.isEmpty() && !linkUrl.isEmpty());
        m_editOps->insertLink(linkText, linkUrl);
    }
}

void VMdEditor::insertTable()
{
    // Get the dialog info.
    VInsertTableDialog td(this);
    if (td.exec() != QDialog::Accepted) {
        return;
    }

    int rowCount = td.getRowCount();
    int colCount = td.getColumnCount();
    VTable::Alignment alignment = td.getAlignment();

    QTextCursor cursor = textCursorW();
    if (cursor.hasSelection()) {
        cursor.clearSelection();
        setTextCursorW(cursor);
    }

    bool newBlock = !cursor.atBlockEnd();
    if (!newBlock && !cursor.atBlockStart()) {
        QString text = cursor.block().text().trimmed();
        if (!text.isEmpty() && text != ">") {
            // Insert a new block before inserting table.
            newBlock = true;
        }
    }

    if (newBlock) {
        VEditUtils::insertBlock(cursor, false);
        VEditUtils::indentBlockAsBlock(cursor, false);
        setTextCursorW(cursor);
    }

    // Insert table right at cursor.
    m_tableHelper->insertTable(rowCount, colCount, alignment);
}

void VMdEditor::initImageHostingMenu(QMenu *p_menu)
{
    QMenu *uploadImageMenu = new QMenu(tr("&Upload Image To"), p_menu);

    // Upload the image to GitHub image hosting.
    QAction *uploadImageToGithub = new QAction(tr("&GitHub"), uploadImageMenu);
    connect(uploadImageToGithub, &QAction::triggered, this, &VMdEditor::requestUploadImageToGithub);
    uploadImageMenu->addAction(uploadImageToGithub);

    // Upload the image to Gitee image hosting.
    QAction *uploadImageToGitee = new QAction(tr("G&itee"), uploadImageMenu);
    connect(uploadImageToGitee, &QAction::triggered, this, &VMdEditor::requestUploadImageToGitee);
    uploadImageMenu->addAction(uploadImageToGitee);

    // Upload the image to Wechat image hosting
    QAction *uploadImageToWechat = new QAction(tr("&Wechat"), uploadImageMenu);
    connect(uploadImageToWechat, &QAction::triggered, this, &VMdEditor::requestUploadImageToWechat);
    uploadImageMenu->addAction(uploadImageToWechat);

    // Upload the image to Tencent image hosting.
    QAction *uploadImageToTencent = new QAction(tr("&Tencent"), uploadImageMenu);
    connect(uploadImageToTencent, &QAction::triggered, this, &VMdEditor::requestUploadImageToTencent);
    uploadImageMenu->addAction(uploadImageToTencent);

    p_menu->addSeparator();
    p_menu->addMenu(uploadImageMenu);
}
