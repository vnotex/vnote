#include <QtWidgets>
#include <QVector>
#include "vedit.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "hgmarkdownhighlighter.h"
#include "vmdeditoperations.h"
#include "vtoc.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

VEdit::VEdit(VFile *p_file, QWidget *p_parent)
    : QTextEdit(p_parent), m_file(p_file), mdHighlighter(NULL)
{
    connect(document(), &QTextDocument::modificationChanged,
            (VFile *)m_file, &VFile::setModified);

    if (m_file->getDocType() == DocType::Markdown) {
        setAcceptRichText(false);
        mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                  500, document());
        connect(mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
                this, &VEdit::generateEditOutline);
        editOps = new VMdEditOperations(this, m_file);
    } else {
        editOps = NULL;
    }

    updateTabSettings();
    updateFontAndPalette();
    connect(this, &VEdit::cursorPositionChanged,
            this, &VEdit::updateCurHeader);
}

VEdit::~VEdit()
{
    if (m_file) {
        disconnect(document(), &QTextDocument::modificationChanged,
                   (VFile *)m_file, &VFile::setModified);
    }
    if (editOps) {
        delete editOps;
        editOps = NULL;
    }
}

void VEdit::updateFontAndPalette()
{
    switch (m_file->getDocType()) {
    case DocType::Markdown:
        setFont(vconfig.getMdEditFont());
        setPalette(vconfig.getMdEditPalette());
        break;
    case DocType::Html:
        setFont(vconfig.getBaseEditFont());
        setPalette(vconfig.getBaseEditPalette());
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
        return;
    }
}

void VEdit::updateTabSettings()
{
    switch (m_file->getDocType()) {
    case DocType::Markdown:
        if (vconfig.getTabStopWidth() > 0) {
            QFontMetrics metrics(vconfig.getMdEditFont());
            setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
        }
        break;
    case DocType::Html:
        if (vconfig.getTabStopWidth() > 0) {
            QFontMetrics metrics(vconfig.getBaseEditFont());
            setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
        }
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
        return;
    }

    isExpandTab = vconfig.getIsExpandTab();
    if (isExpandTab && (vconfig.getTabStopWidth() > 0)) {
        tabSpaces = QString(vconfig.getTabStopWidth(), ' ');
    }
}

void VEdit::beginEdit()
{
    updateTabSettings();
    updateFontAndPalette();
    switch (m_file->getDocType()) {
    case DocType::Html:
        setHtml(m_file->getContent());
        break;
    case DocType::Markdown:
        setFont(vconfig.getMdEditFont());
        setPlainText(m_file->getContent());
        initInitImages();
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
    }
    setReadOnly(false);
    setModified(false);
}

void VEdit::endEdit()
{
    setReadOnly(true);
    if (m_file->getDocType() == DocType::Markdown) {
        clearUnusedImages();
    }
}

void VEdit::saveFile()
{
    if (!document()->isModified()) {
        return;
    }

    switch (m_file->getDocType()) {
    case DocType::Html:
        m_file->setContent(toHtml());
        break;
    case DocType::Markdown:
        m_file->setContent(toPlainText());
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
    }
    document()->setModified(false);
}

void VEdit::reloadFile()
{
    switch (m_file->getDocType()) {
    case DocType::Html:
        setHtml(m_file->getContent());
        break;
    case DocType::Markdown:
        setPlainText(m_file->getContent());
        break;
    default:
        qWarning() << "error: unknown doc type" << int(m_file->getDocType());
    }
    setModified(false);
}

void VEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Tab) && isExpandTab) {
        QTextCursor cursor(document());
        cursor.setPosition(textCursor().position());
        cursor.insertText(tabSpaces);
        return;
    }
    QTextEdit::keyPressEvent(event);
}

bool VEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls()
           || QTextEdit::canInsertFromMimeData(source);
}

void VEdit::insertFromMimeData(const QMimeData *source)
{
    if (source->hasImage()) {
        // Image data in the clipboard
        if (editOps) {
            bool ret = editOps->insertImageFromMimeData(source);
            if (ret) {
                return;
            }
        }
    } else if (source->hasUrls()) {
        // Paste an image file
        if (editOps) {
            bool ret = editOps->insertURLFromMimeData(source);
            if (ret) {
                return;
            }
        }
    }
    QTextEdit::insertFromMimeData(source);
}

void VEdit::generateEditOutline()
{
    QTextDocument *doc = document();
    headers.clear();
    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg("(#{1,6})\\s*(\\S.*)");  // Need to trim the spaces
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        Q_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::BlockNormal) &&
            headerReg.exactMatch(block.text())) {
            VHeader header(headerReg.cap(1).length(),
                           headerReg.cap(2).trimmed(), "", block.firstLineNumber());
            headers.append(header);
        }
    }

    emit headersChanged(headers);
    updateCurHeader();
}

void VEdit::scrollToLine(int lineNumber)
{
    Q_ASSERT(lineNumber >= 0);

    // Move the cursor to the end first
    moveCursor(QTextCursor::End);
    QTextCursor cursor(document()->findBlockByLineNumber(lineNumber));
    cursor.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(cursor);

    setFocus();
}

void VEdit::updateCurHeader()
{
    int curHeader = 0;
    QTextCursor cursor(this->textCursor());
    int curLine = cursor.block().firstLineNumber();
    for (int i = headers.size() - 1; i >= 0; --i) {
        if (headers[i].lineNumber <= curLine) {
            curHeader = headers[i].lineNumber;
            break;
        }
    }
    emit curHeaderChanged(curHeader);
}

void VEdit::insertImage(const QString &name)
{
    m_insertedImages.append(name);
}

void VEdit::initInitImages()
{
    m_initImages = VUtils::imagesFromMarkdownFile(m_file->retrivePath());
}

void VEdit::clearUnusedImages()
{
    QVector<QString> images = VUtils::imagesFromMarkdownFile(m_file->retrivePath());

    if (!m_insertedImages.isEmpty()) {
        QVector<QString> imageNames(images.size());
        for (int i = 0; i < imageNames.size(); ++i) {
            imageNames[i] = VUtils::fileNameFromPath(images[i]);
        }

        QDir dir = QDir(m_file->retriveImagePath());
        for (int i = 0; i < m_insertedImages.size(); ++i) {
            QString name = m_insertedImages[i];
            int j;
            for (j = 0; j < imageNames.size(); ++j) {
                if (name == imageNames[j]) {
                    break;
                }
            }

            // Delete it
            if (j == imageNames.size()) {
                QString imagePath = dir.filePath(name);
                QFile(imagePath).remove();
                qDebug() << "delete inserted image" << imagePath;
            }
        }
        m_insertedImages.clear();
    }

    for (int i = 0; i < m_initImages.size(); ++i) {
        QString imagePath = m_initImages[i];
        int j;
        for (j = 0; j < images.size(); ++j) {
            if (imagePath == images[j]) {
                break;
            }
        }

        // Delete it
        if (j == images.size()) {
            QFile(imagePath).remove();
            qDebug() << "delete existing image" << imagePath;
        }
    }
    m_initImages.clear();
}
