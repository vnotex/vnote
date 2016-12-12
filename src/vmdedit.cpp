#include <QtWidgets>
#include "vmdedit.h"
#include "hgmarkdownhighlighter.h"
#include "vmdeditoperations.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

VMdEdit::VMdEdit(VFile *p_file, QWidget *p_parent)
    : VEdit(p_file, p_parent), m_mdHighlighter(NULL)
{
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);

    setAcceptRichText(false);
    m_mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                500, document());
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, &VMdEdit::generateEditOutline);
    m_editOps = new VMdEditOperations(this, m_file);

    connect(this, &VMdEdit::cursorPositionChanged,
            this, &VMdEdit::updateCurHeader);

    updateTabSettings();
    updateFontAndPalette();
}

void VMdEdit::updateFontAndPalette()
{
    setFont(vconfig.getMdEditFont());
    setPalette(vconfig.getMdEditPalette());
}

void VMdEdit::updateTabSettings()
{
    if (vconfig.getTabStopWidth() > 0) {
        QFontMetrics metrics(vconfig.getMdEditFont());
        setTabStopWidth(vconfig.getTabStopWidth() * metrics.width(' '));
    }
    m_expandTab = vconfig.getIsExpandTab();
    if (m_expandTab && (vconfig.getTabStopWidth() > 0)) {
        m_tabSpaces = QString(vconfig.getTabStopWidth(), ' ');
    }
}

void VMdEdit::beginEdit()
{
    updateTabSettings();
    updateFontAndPalette();

    setFont(vconfig.getMdEditFont());

    Q_ASSERT(m_file->getContent() == toPlainText());

    initInitImages();

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
    m_file->setContent(toPlainText());
    document()->setModified(false);
}

void VMdEdit::reloadFile()
{
    setPlainText(m_file->getContent());
    setModified(false);
}

void VMdEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Tab) && m_expandTab) {
        QTextCursor cursor(document());
        cursor.setPosition(textCursor().position());
        cursor.insertText(m_tabSpaces);
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
    if (source->hasImage()) {
        // Image data in the clipboard
        bool ret = m_editOps->insertImageFromMimeData(source);
        if (ret) {
            return;
        }
    } else if (source->hasUrls()) {
        // Paste an image file
        bool ret = m_editOps->insertURLFromMimeData(source);
        if (ret) {
            return;
        }
    }
    VEdit::insertFromMimeData(source);
}

void VMdEdit::imageInserted(const QString &p_name)
{
    m_insertedImages.append(p_name);
}

void VMdEdit::initInitImages()
{
    m_initImages = VUtils::imagesFromMarkdownFile(m_file->retrivePath());
}

void VMdEdit::clearUnusedImages()
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
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        Q_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::BlockNormal) &&
            headerReg.exactMatch(block.text())) {
            VHeader header(headerReg.cap(1).length(),
                           headerReg.cap(2).trimmed(), "", block.firstLineNumber());
            m_headers.append(header);
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
