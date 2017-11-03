#include "vmdeditor.h"

#include <QtWidgets>
#include <QMenu>
#include <QDebug>

#include "vdocument.h"
#include "utils/veditutils.h"
#include "vedittab.h"
#include "hgmarkdownhighlighter.h"
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

extern VConfigManager *g_config;

VMdEditor::VMdEditor(VFile *p_file,
                     VDocument *p_doc,
                     MarkdownConverterType p_type,
                     QWidget *p_parent)
    : VTextEdit(p_parent),
      VEditor(p_file, this),
      m_mdHighlighter(NULL),
      m_freshEdit(true)
{
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);

    VEditor::init();

    // Hook functions from VEditor.
    connect(this, &VTextEdit::cursorPositionChanged,
            this, [this]() {
                highlightOnCursorPositionChanged();
            });

    connect(this, &VTextEdit::selectionChanged,
            this, [this]() {
                highlightSelectedWord();
            });
    // End.

    m_mdHighlighter = new HGMarkdownHighlighter(g_config->getMdHighlightingStyles(),
                                                g_config->getCodeBlockStyles(),
                                                g_config->getMarkdownHighlightInterval(),
                                                document());

    connect(m_mdHighlighter, &HGMarkdownHighlighter::headersUpdated,
            this, &VMdEditor::updateHeaders);

    // After highlight, the cursor may trun into non-visible. We should make it visible
    // in this case.
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, [this]() {
            makeBlockVisible(textCursor().block());
    });

    m_cbHighlighter = new VCodeBlockHighlightHelper(m_mdHighlighter,
                                                    p_doc,
                                                    p_type);

    m_previewMgr = new VPreviewManager(this);
    connect(m_mdHighlighter, &HGMarkdownHighlighter::imageLinksUpdated,
            m_previewMgr, &VPreviewManager::imageLinksUpdated);
    connect(m_previewMgr, &VPreviewManager::requestUpdateImageLinks,
            m_mdHighlighter, &HGMarkdownHighlighter::updateHighlight);

    m_editOps = new VMdEditOperations(this, m_file);
    connect(m_editOps, &VEditOperations::statusMessage,
            m_object, &VEditorObject::statusMessage);
    connect(m_editOps, &VEditOperations::vimStatusUpdated,
            m_object, &VEditorObject::vimStatusUpdated);

    connect(this, &VTextEdit::cursorPositionChanged,
            this, &VMdEditor::updateCurrentHeader);

    updateFontAndPalette();

    updateConfig();
}

void VMdEditor::updateFontAndPalette()
{
    setFont(g_config->getMdEditFont());
    setPalette(g_config->getMdEditPalette());
}

void VMdEditor::beginEdit()
{
    updateFontAndPalette();

    updateConfig();

    initInitImages();

    setModified(false);

    setReadOnlyAndHighlightCurrentLine(false);

    emit statusChanged();

    updateHeaders(m_mdHighlighter->getHeaderRegions());

    if (m_freshEdit) {
        m_freshEdit = false;
        emit m_object->ready();
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
}

void VMdEditor::reloadFile()
{
    const QString &content = m_file->getContent();
    setPlainText(content);

    setModified(false);
}

bool VMdEditor::scrollToBlock(int p_blockNumber)
{
    QTextBlock block = document()->findBlockByNumber(p_blockNumber);
    if (block.isValid()) {
        VEditUtils::scrollBlockInPage(this, block.blockNumber(), 0);
        moveCursor(QTextCursor::EndOfBlock);
        return true;
    }

    return false;
}

// Get the visual offset of a block.
#define GETVISUALOFFSETY (contentOffsetY() + (int)rect.y())

void VMdEditor::makeBlockVisible(const QTextBlock &p_block)
{
    if (!p_block.isValid() || !p_block.isVisible()) {
        return;
    }

    QScrollBar *vbar = verticalScrollBar();
    if (!vbar || !vbar->isVisible()) {
        // No vertical scrollbar. No need to scroll.
        return;
    }

    int height = rect().height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible()) {
        height -= hbar->height();
    }

    bool moved = false;

    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    QRectF rect = layout->blockBoundingRect(p_block);
    int y = GETVISUALOFFSETY;
    int rectHeight = (int)rect.height();

    // Handle the case rectHeight >= height.
    if (rectHeight >= height) {
        if (y < 0) {
            // Need to scroll up.
            while (y + rectHeight < height && vbar->value() > vbar->minimum()) {
                moved = true;
                vbar->setValue(vbar->value() - vbar->singleStep());
                rect = layout->blockBoundingRect(p_block);
                rectHeight = (int)rect.height();
                y = GETVISUALOFFSETY;
            }
        } else if (y > 0) {
            // Need to scroll down.
            while (y > 0 && vbar->value() < vbar->maximum()) {
                moved = true;
                vbar->setValue(vbar->value() + vbar->singleStep());
                rect = layout->blockBoundingRect(p_block);
                rectHeight = (int)rect.height();
                y = GETVISUALOFFSETY;
            }

            if (y < 0) {
                // One step back.
                moved = true;
                vbar->setValue(vbar->value() - vbar->singleStep());
            }
        }

        if (moved) {
            qDebug() << "scroll to make huge block visible";
        }

        return;
    }

    while (y < 0 && vbar->value() > vbar->minimum()) {
        qDebug() << y << vbar->value() << vbar->minimum() << rectHeight;
        moved = true;
        vbar->setValue(vbar->value() - vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        rectHeight = (int)rect.height();
        y = GETVISUALOFFSETY;
    }

    if (moved) {
        qDebug() << "scroll page down to make block visible";
        return;
    }

    while (y + rectHeight > height && vbar->value() < vbar->maximum()) {
        moved = true;
        vbar->setValue(vbar->value() + vbar->singleStep());
        rect = layout->blockBoundingRect(p_block);
        rectHeight = (int)rect.height();
        y = GETVISUALOFFSETY;
    }

    if (moved) {
        qDebug() << "scroll page up to make block visible";
    }
}

void VMdEditor::contextMenuEvent(QContextMenuEvent *p_event)
{
    QMenu *menu = createStandardContextMenu();
    menu->setToolTipsVisible(true);

    const QList<QAction *> actions = menu->actions();

    if (!textCursor().hasSelection()) {
        VEditTab *editTab = dynamic_cast<VEditTab *>(parent());
        Q_ASSERT(editTab);
        if (editTab->isEditMode()) {
            QAction *saveExitAct = new QAction(QIcon(":/resources/icons/save_exit.svg"),
                                               tr("&Save Changes And Read"),
                                               menu);
            saveExitAct->setToolTip(tr("Save changes and exit edit mode"));
            connect(saveExitAct, &QAction::triggered,
                    this, [this]() {
                        emit m_object->saveAndRead();
                    });

            QAction *discardExitAct = new QAction(QIcon(":/resources/icons/discard_exit.svg"),
                                                  tr("&Discard Changes And Read"),
                                                  menu);
            discardExitAct->setToolTip(tr("Discard changes and exit edit mode"));
            connect(discardExitAct, &QAction::triggered,
                    this, [this]() {
                        emit m_object->discardAndRead();
                    });

            menu->insertAction(actions.isEmpty() ? NULL : actions[0], discardExitAct);
            menu->insertAction(discardExitAct, saveExitAct);
            if (!actions.isEmpty()) {
                menu->insertSeparator(actions[0]);
            }
        }
    }

    menu->exec(p_event->globalPos());
    delete menu;
}

void VMdEditor::mousePressEvent(QMouseEvent *p_event)
{
    if (handleMousePressEvent(p_event)) {
        return;
    }

    VTextEdit::mousePressEvent(p_event);

    emit m_object->selectionChangedByMouse(textCursor().hasSelection());
}

void VMdEditor::mouseReleaseEvent(QMouseEvent *p_event)
{
    if (handleMouseReleaseEvent(p_event)) {
        return;
    }

    VTextEdit::mousePressEvent(p_event);
}

void VMdEditor::mouseMoveEvent(QMouseEvent *p_event)
{
    if (handleMouseMoveEvent(p_event)) {
        return;
    }

    VTextEdit::mouseMoveEvent(p_event);

    emit m_object->selectionChangedByMouse(textCursor().hasSelection());
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
    QRectF rect = layout->blockBoundingRect(p_block);
    int y = GETVISUALOFFSETY;
    int rectHeight = (int)rect.height();

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

static void insertSequenceToHeader(QTextBlock p_block,
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

    QTextCursor cursor(p_block);
    cursor.setPosition(p_block.position() + start);
    if (start != end) {
        cursor.setPosition(p_block.position() + end, QTextCursor::KeepAnchor);
    }

    if (p_seq.isEmpty()) {
        cursor.removeSelectedText();
    } else {
        cursor.insertText(p_seq + ' ');
    }
}

void VMdEditor::updateHeaders(const QVector<VElementRegion> &p_headerRegions)
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
            continue;
        }

        if ((block.userState() == HighlightBlockState::Normal)
            && headerReg.exactMatch(block.text())) {
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
    for (int i = 0; i < headers.size(); ++i) {
        VTableOfContentItem &item = headers[i];
        while (item.m_level > curLevel + 1) {
            curLevel += 1;

            // Insert empty level which is an invalid header.
            m_headers.append(VTableOfContentItem(c_emptyHeaderName,
                                                 curLevel,
                                                 -1,
                                                 m_headers.size()));
            if (autoSequence) {
                addHeaderSequence(seqs, curLevel, headingSequenceBaseLevel);
            }
        }

        item.m_index = m_headers.size();
        m_headers.append(item);
        curLevel = item.m_level;
        if (autoSequence) {
            addHeaderSequence(seqs, item.m_level, headingSequenceBaseLevel);

            QString seqStr = headerSequenceStr(seqs);
            if (headerSequences[i] != seqStr) {
                // Insert correct sequence.
                insertSequenceToHeader(doc->findBlockByNumber(headerBlockNumbers[i]),
                                       headerReg,
                                       preReg,
                                       seqStr);
            }
        }
    }

    emit headersChanged(m_headers);

    updateCurrentHeader();
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

    QVector<QString> unusedImages;

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
                unusedImages.push_back(link.m_path);
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
            unusedImages.push_back(link.m_path);
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
                    unusedImages.push_back(item.m_name);
                }
            }
        }

        for (int i = 0; i < unusedImages.size(); ++i) {
            bool ret = false;
            if (m_file->getType() == FileType::Note) {
                const VNoteFile *tmpFile = dynamic_cast<const VNoteFile *>((VFile *)m_file);
                ret = VUtils::deleteFile(tmpFile->getNotebook(), unusedImages[i], false);
            } else if (m_file->getType() == FileType::Orphan) {
                const VOrphanFile *tmpFile = dynamic_cast<const VOrphanFile *>((VFile *)m_file);
                ret = VUtils::deleteFile(tmpFile, unusedImages[i], false);
            } else {
                Q_ASSERT(false);
            }

            if (!ret) {
                qWarning() << "fail to delete unused original image" << unusedImages[i];
            } else {
                qDebug() << "delete unused image" << unusedImages[i];
            }
        }
    }

    m_initImages.clear();
}

void VMdEditor::keyPressEvent(QKeyEvent *p_event)
{
    if (m_editOps && m_editOps->handleKeyPressEvent(p_event)) {
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
    VSelectDialog dialog(tr("Insert From Clipboard"), this);
    dialog.addSelection(tr("Insert As Image"), 0);
    dialog.addSelection(tr("Insert As Text"), 1);

    if (p_source->hasImage()) {
        // Image data in the clipboard
        if (p_source->hasText()) {
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.getSelection() == 1) {
                    // Insert as text.
                    Q_ASSERT(p_source->hasText() && p_source->hasImage());
                    VTextEdit::insertFromMimeData(p_source);
                    return;
                }
            } else {
                return;
            }
        }

        m_editOps->insertImageFromMimeData(p_source);
        return;
    } else if (p_source->hasUrls()) {
        QList<QUrl> urls = p_source->urls();
        if (urls.size() == 1 && VUtils::isImageURL(urls[0])) {
            if (dialog.exec() == QDialog::Accepted) {
                // FIXME: After calling dialog.exec(), p_source->hasUrl() returns false.
                if (dialog.getSelection() == 0) {
                    // Insert as image.
                    m_editOps->insertImageFromURL(urls[0]);
                    return;
                }

                QMimeData newSource;
                newSource.setUrls(urls);
                VTextEdit::insertFromMimeData(&newSource);
                return;
            } else {
                return;
            }
        }
    } else if (p_source->hasText()) {
        QString text = p_source->text();
        if (VUtils::isImageURLText(text)) {
            // The text is a URL to an image.
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.getSelection() == 0) {
                    // Insert as image.
                    QUrl url(text);
                    if (url.isValid()) {
                        m_editOps->insertImageFromURL(QUrl(text));
                    }
                    return;
                }
            } else {
                return;
            }
        }

        Q_ASSERT(p_source->hasText());
    }

    VTextEdit::insertFromMimeData(p_source);
}

void VMdEditor::imageInserted(const QString &p_path)
{
    ImageLink link;
    link.m_path = p_path;
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

void VMdEditor::scrollBlockInPage(int p_blockNum, int p_dest)
{
    VEditUtils::scrollBlockInPage(this, p_blockNum, p_dest);
}

void VMdEditor::updateTextEditConfig()
{
    m_previewMgr->setPreviewEnabled(g_config->getEnablePreviewImages());
    setBlockImageEnabled(g_config->getEnablePreviewImages());

    setImageWidthConstrainted(g_config->getEnablePreviewImageConstraint());

    setLineLeading(m_config.m_lineDistanceHeight);

    setImageLineColor(g_config->getEditorPreviewImageLineFg());

    int lineNumber = g_config->getEditorLineNumber();
    if (lineNumber < (int)LineNumberType::None || lineNumber >= (int)LineNumberType::Invalid) {
        lineNumber = (int)LineNumberType::None;
    }

    setLineNumberType((LineNumberType)lineNumber);
    setLineNumberColor(g_config->getEditorLineNumberFg(),
                       g_config->getEditorLineNumberBg());
}

void VMdEditor::updateConfig()
{
    updateEditConfig();
    updateTextEditConfig();
}
