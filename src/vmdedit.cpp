#include <QtWidgets>
#include "vmdedit.h"
#include "hgmarkdownhighlighter.h"
#include "vcodeblockhighlighthelper.h"
#include "vmdeditoperations.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "dialog/vselectdialog.h"
#include "vimagepreviewer.h"

extern VConfigManager vconfig;
extern VNote *g_vnote;

VMdEdit::VMdEdit(VFile *p_file, VDocument *p_vdoc, MarkdownConverterType p_type,
                 QWidget *p_parent)
    : VEdit(p_file, p_parent), m_mdHighlighter(NULL)
{
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);

    setAcceptRichText(false);
    m_mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                vconfig.getCodeBlockStyles(),
                                                700, document());
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, &VMdEdit::generateEditOutline);

    m_cbHighlighter = new VCodeBlockHighlightHelper(m_mdHighlighter, p_vdoc,
                                                    p_type);

    m_imagePreviewer = new VImagePreviewer(this, 500);

    m_editOps = new VMdEditOperations(this, m_file);
    connect(m_editOps, &VEditOperations::keyStateChanged,
            this, &VMdEdit::handleEditStateChanged);

    connect(this, &VMdEdit::cursorPositionChanged,
            this, &VMdEdit::updateCurHeader);

    connect(this, &VMdEdit::selectionChanged,
            this, &VMdEdit::handleSelectionChanged);
    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &VMdEdit::handleClipboardChanged);

    m_editOps->updateTabSettings();
    updateFontAndPalette();
}

void VMdEdit::updateFontAndPalette()
{
    setFont(vconfig.getMdEditFont());
    setPalette(vconfig.getMdEditPalette());
    m_cursorLineColor = vconfig.getEditorCurrentLineBackground();
}

void VMdEdit::beginEdit()
{
    m_editOps->updateTabSettings();
    updateFontAndPalette();

    Q_ASSERT(m_file->getContent() == toPlainTextWithoutImg());

    initInitImages();

    m_imagePreviewer->refresh();

    setReadOnly(false);
    setModified(false);

    // Request update outline.
    generateEditOutline();
}

void VMdEdit::endEdit()
{
    setReadOnly(true);
    clearUnusedImages();
}

void VMdEdit::saveFile()
{
    if (!document()->isModified()) {
        return;
    }
    m_file->setContent(toPlainTextWithoutImg());
    document()->setModified(false);
}

void VMdEdit::reloadFile()
{
    const QString &content = m_file->getContent();
    V_ASSERT(content.indexOf(QChar::ObjectReplacementCharacter) == -1);
    setPlainText(content);
    setModified(false);
}

void VMdEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_editOps->handleKeyPressEvent(event)) {
        return;
    }
    VEdit::keyPressEvent(event);
}

bool VMdEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls()
           || VEdit::canInsertFromMimeData(source);
}

void VMdEdit::insertFromMimeData(const QMimeData *source)
{
    VSelectDialog dialog(tr("Insert From Clipboard"), this);
    dialog.addSelection(tr("Insert As Image"), 0);
    dialog.addSelection(tr("Insert As Text"), 1);

    if (source->hasImage()) {
        // Image data in the clipboard
        if (source->hasText()) {
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.getSelection() == 1) {
                    // Insert as text.
                    Q_ASSERT(source->hasText() && source->hasImage());
                    VEdit::insertFromMimeData(source);
                    return;
                }
            } else {
                return;
            }
        }
        m_editOps->insertImageFromMimeData(source);
        return;
    } else if (source->hasUrls()) {
        QList<QUrl> urls = source->urls();
        if (urls.size() == 1 && VUtils::isImageURL(urls[0])) {
            if (dialog.exec() == QDialog::Accepted) {
                // FIXME: After calling dialog.exec(), source->hasUrl() returns false.
                if (dialog.getSelection() == 0) {
                    // Insert as image.
                    m_editOps->insertImageFromURL(urls[0]);
                    return;
                }
                QMimeData newSource;
                newSource.setUrls(urls);
                VEdit::insertFromMimeData(&newSource);
                return;
            } else {
                return;
            }
        }
    } else if (source->hasText()) {
        QString text = source->text();
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
        Q_ASSERT(source->hasText());
    }
    VEdit::insertFromMimeData(source);
}

void VMdEdit::imageInserted(const QString &p_path)
{
    ImageLink link;
    link.m_path = p_path;
    link.m_type = ImageLink::LocalRelativeInternal;

    m_insertedImages.append(link);
}

void VMdEdit::initInitImages()
{
    m_initImages = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                       ImageLink::LocalRelativeInternal);
}

void VMdEdit::clearUnusedImages()
{
    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                                    ImageLink::LocalRelativeInternal);

    if (!m_insertedImages.isEmpty()) {
        for (int i = 0; i < m_insertedImages.size(); ++i) {
            const ImageLink &link = m_insertedImages[i];

            V_ASSERT(link.m_type == ImageLink::LocalRelativeInternal);

            int j;
            for (j = 0; j < images.size(); ++j) {
                if (link.m_path == images[j].m_path) {
                    break;
                }
            }

            // This inserted image is no longer in the file.
            if (j == images.size()) {
                if (!QFile(link.m_path).remove()) {
                    qWarning() << "fail to delete unused inserted image" << link.m_path;
                } else {
                    qDebug() << "delete unused inserted image" << link.m_path;
                }
            }
        }

        m_insertedImages.clear();
    }

    for (int i = 0; i < m_initImages.size(); ++i) {
        const ImageLink &link = m_initImages[i];

        V_ASSERT(link.m_type == ImageLink::LocalRelativeInternal);

        int j;
        for (j = 0; j < images.size(); ++j) {
            if (link.m_path == images[j].m_path) {
                break;
            }
        }

        // Original local relative image is no longer in the file.
        if (j == images.size()) {
            if (!QFile(link.m_path).remove()) {
                qWarning() << "fail to delete unused original image" << link.m_path;
            } else {
                qDebug() << "delete unused original image" << link.m_path;
            }
        }
    }

    m_initImages.clear();
}

void VMdEdit::updateCurHeader()
{
    int curHeader = 0;
    QTextCursor cursor(this->textCursor());
    int curLine = cursor.block().firstLineNumber();
    int i = 0;
    for (i = m_headers.size() - 1; i >= 0; --i) {
        if (m_headers[i].lineNumber <= curLine) {
            curHeader = m_headers[i].lineNumber;
            break;
        }
    }
    emit curHeaderChanged(curHeader, i == -1 ? 0 : i);
}

void VMdEdit::generateEditOutline()
{
    QTextDocument *doc = document();
    m_headers.clear();
    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg("(#{1,6})\\s*(\\S.*)");  // Need to trim the spaces
    int lastLevel = 0;
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        Q_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::Normal) &&
            headerReg.exactMatch(block.text())) {
            int level = headerReg.cap(1).length();
            VHeader header(level, headerReg.cap(2).trimmed(),
                           "", block.firstLineNumber());
            while (level > lastLevel + 1) {
                // Insert empty level.
                m_headers.append(VHeader(++lastLevel, "[EMPTY]",
                                         "", block.firstLineNumber()));
            }
            m_headers.append(header);
            lastLevel = level;
        }
    }

    emit headersChanged(m_headers);
    updateCurHeader();
}

void VMdEdit::scrollToHeader(int p_headerIndex)
{
    Q_ASSERT(p_headerIndex >= 0);
    if (p_headerIndex < m_headers.size()) {
        int line = m_headers[p_headerIndex].lineNumber;
        qDebug() << "scroll editor to" << p_headerIndex << "line" << line;
        scrollToLine(line);
    }
}

QString VMdEdit::toPlainTextWithoutImg() const
{
    QString text = toPlainText();
    int start = 0;
    do {
        int index = text.indexOf(QChar::ObjectReplacementCharacter, start);
        if (index == -1) {
            break;
        }
        start = removeObjectReplacementLine(text, index);
    } while (start > -1 && start < text.size());
    return text;
}

int VMdEdit::removeObjectReplacementLine(QString &p_text, int p_index) const
{
    Q_ASSERT(p_text.size() > p_index && p_text.at(p_index) == QChar::ObjectReplacementCharacter);
    int prevLineIdx = p_text.lastIndexOf('\n', p_index);
    if (prevLineIdx == -1) {
        prevLineIdx = 0;
    }
    // Remove [\n....?]
    p_text.remove(prevLineIdx, p_index - prevLineIdx + 1);
    return prevLineIdx - 1;
}

void VMdEdit::handleEditStateChanged(KeyState p_state)
{
    qDebug() << "edit state" << (int)p_state;
    if (p_state == KeyState::Normal) {
        m_cursorLineColor = vconfig.getEditorCurrentLineBackground();
    } else {
        m_cursorLineColor = vconfig.getEditorCurrentLineVimBackground();
    }
    highlightCurrentLine();
}

void VMdEdit::handleSelectionChanged()
{
    if (!vconfig.getEnablePreviewImages()) {
        return;
    }

    QString text = textCursor().selectedText();
    if (text.isEmpty() && !m_imagePreviewer->isPreviewEnabled()) {
        m_imagePreviewer->enableImagePreview();
    } else if (m_imagePreviewer->isPreviewEnabled()) {
        if (text.trimmed() == QString(QChar::ObjectReplacementCharacter)) {
            // Select the image and some whitespaces.
            // We can let the user copy the image.
            return;
        } else if (text.contains(QChar::ObjectReplacementCharacter)) {
            m_imagePreviewer->disableImagePreview();
        }
    }
}

void VMdEdit::handleClipboardChanged(QClipboard::Mode p_mode)
{
    if (!hasFocus()) {
        return;
    }
    if (p_mode == QClipboard::Clipboard) {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            QString text = mimeData->text();
            if (clipboard->ownsClipboard() &&
                (text.trimmed() == QString(QChar::ObjectReplacementCharacter))) {
                QImage image = selectedImage();
                clipboard->clear(QClipboard::Clipboard);
                if (!image.isNull()) {
                    clipboard->setImage(image, QClipboard::Clipboard);
                }
            }
        }
    }
}

QImage VMdEdit::selectedImage()
{
    QImage image;
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return image;
    }
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    QTextDocument *doc = document();
    QTextBlock startBlock = doc->findBlock(start);
    QTextBlock endBlock = doc->findBlock(end);
    QTextBlock block = startBlock;
    while (block.isValid()) {
        if (m_imagePreviewer->isImagePreviewBlock(block)) {
            image = m_imagePreviewer->fetchCachedImageFromPreviewBlock(block);
            break;
        }
        if (block == endBlock) {
            break;
        }
        block = block.next();
    }
    return image;
}

void VMdEdit::resizeEvent(QResizeEvent *p_event)
{
    m_imagePreviewer->update();

    VEdit::resizeEvent(p_event);
}
