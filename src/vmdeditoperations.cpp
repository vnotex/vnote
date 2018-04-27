#include <QtDebug>
#include <QImage>
#include <QVariant>
#include <QMimeData>
#include <QWidget>
#include <QImageReader>
#include <QDir>
#include <QMessageBox>
#include <QKeyEvent>
#include <QTextCursor>
#include <QTimer>
#include <QGuiApplication>
#include <QApplication>
#include <QClipboard>
#include "vmdeditoperations.h"
#include "dialog/vinsertimagedialog.h"
#include "dialog/vselectdialog.h"
#include "utils/vutils.h"
#include "veditor.h"
#include "vdownloader.h"
#include "vfile.h"
#include "vmdeditor.h"
#include "vconfigmanager.h"
#include "utils/vvim.h"
#include "utils/veditutils.h"

extern VConfigManager *g_config;

const QString VMdEditOperations::c_defaultImageTitle = "";

VMdEditOperations::VMdEditOperations(VEditor *p_editor, VFile *p_file)
    : VEditOperations(p_editor, p_file), m_autoIndentPos(-1)
{
}

bool VMdEditOperations::insertImageFromMimeData(const QMimeData *source)
{
    QImage image = qvariant_cast<QImage>(source->imageData());
    if (image.isNull()) {
        return false;
    }

    VInsertImageDialog dialog(tr("Insert Image From Clipboard"),
                              c_defaultImageTitle,
                              "",
                              false,
                              m_editor->getEditor());
    dialog.setImage(image);
    if (dialog.exec() == QDialog::Accepted) {
        insertImageFromQImage(dialog.getImageTitleInput(),
                              m_file->fetchImageFolderPath(),
                              m_file->getImageFolderInLink(),
                              image);
    }

    return true;
}

void VMdEditOperations::insertImageFromQImage(const QString &title, const QString &path,
                                              const QString &folderInLink, const QImage &image)
{
    QString fileName = VUtils::generateImageFileName(path, title);
    QString filePath = QDir(path).filePath(fileName);
    V_ASSERT(!QFile(filePath).exists());

    QString errStr;
    bool ret = VUtils::makePath(path);
    if (!ret) {
        errStr = tr("Fail to create image folder <span style=\"%1\">%2</span>.")
                   .arg(g_config->c_dataTextStyle).arg(path);
    } else {
        ret = image.save(filePath);
        if (!ret) {
            errStr = tr("Fail to save image <span style=\"%1\">%2</span>.")
                       .arg(g_config->c_dataTextStyle).arg(filePath);
        }
    }

    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to insert image <span style=\"%1\">%2</span>.").arg(g_config->c_dataTextStyle).arg(title),
                            errStr,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            m_editor->getEditor());
        return;
    }

    QString url = QString("%1/%2").arg(folderInLink).arg(fileName);
    QString md = QString("![%1](%2)").arg(title).arg(url);
    insertTextAtCurPos(md);

    qDebug() << "insert image" << title << filePath;

    VMdEditor *mdEditor = dynamic_cast<VMdEditor *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(filePath, url);
}

void VMdEditOperations::insertImageFromPath(const QString &title, const QString &path,
                                            const QString &folderInLink, const QString &oriImagePath)
{
    QString fileName = VUtils::generateImageFileName(path, title, QFileInfo(oriImagePath).suffix());
    QString filePath = QDir(path).filePath(fileName);
    V_ASSERT(!QFile(filePath).exists());

    QString errStr;
    bool ret = VUtils::makePath(path);
    if (!ret) {
        errStr = tr("Fail to create image folder <span style=\"%1\">%2</span>.")
                   .arg(g_config->c_dataTextStyle).arg(path);
    } else {
        ret = QFile::copy(oriImagePath, filePath);
        if (!ret) {
            errStr = tr("Fail to copy image <span style=\"%1\">%2</span>.")
                       .arg(g_config->c_dataTextStyle).arg(filePath);
        }
    }

    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to insert image <span style=\"%1\">%2</span>.").arg(g_config->c_dataTextStyle).arg(title),
                            errStr,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            m_editor->getEditor());
        return;
    }

    QString url = QString("%1/%2").arg(folderInLink).arg(fileName);
    QString md = QString("![%1](%2)").arg(title).arg(url);
    insertTextAtCurPos(md);

    qDebug() << "insert image" << title << filePath;

    VMdEditor *mdEditor = dynamic_cast<VMdEditor *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(filePath, url);
}

bool VMdEditOperations::insertImageFromURL(const QUrl &imageUrl)
{
    QString imagePath;
    QImage image;
    bool isLocal = imageUrl.isLocalFile();
    QString title;

    // Whether it is a local file or web URL
    if (isLocal) {
        imagePath = imageUrl.toLocalFile();
        image = VUtils::imageFromFile(imagePath);
        if (image.isNull()) {
            qWarning() << "inserted image is null" << imagePath;
            return false;
        }

        title = "Insert Image From File";
    } else {
        imagePath = imageUrl.toString();
        title = "Insert Image From Network";
    }


    VInsertImageDialog dialog(title,
                              c_defaultImageTitle,
                              imagePath,
                              false,
                              m_editor->getEditor());
    if (isLocal) {
        dialog.setImage(image);
    } else {
        // Download it to a QImage
        VDownloader *downloader = new VDownloader(&dialog);
        connect(downloader, &VDownloader::downloadFinished,
                &dialog, &VInsertImageDialog::imageDownloaded);
        downloader->download(imageUrl.toString());
    }
    if (dialog.exec() == QDialog::Accepted) {
        if (isLocal) {
            insertImageFromPath(dialog.getImageTitleInput(),
                                m_file->fetchImageFolderPath(),
                                m_file->getImageFolderInLink(),
                                imagePath);
        } else {
            insertImageFromQImage(dialog.getImageTitleInput(),
                                  m_file->fetchImageFolderPath(),
                                  m_file->getImageFolderInLink(),
                                  dialog.getImage());
        }
    }
    return true;
}

bool VMdEditOperations::insertImage()
{
    // Use empty title and path to let the dialog auto complete.
    VInsertImageDialog dialog(tr("Insert Image"),
                              "",
                              "",
                              true,
                              m_editor->getEditor());
    if (dialog.exec() == QDialog::Accepted) {
        VInsertImageDialog::ImageType type = dialog.getImageType();
        if (type == VInsertImageDialog::ImageType::LocalFile) {
            insertImageFromPath(dialog.getImageTitleInput(),
                                m_file->fetchImageFolderPath(),
                                m_file->getImageFolderInLink(),
                                dialog.getPathInput());
        } else {
            QImage img = dialog.getImage();
            if (!img.isNull()) {
                insertImageFromQImage(dialog.getImageTitleInput(),
                                      m_file->fetchImageFolderPath(),
                                      m_file->getImageFolderInLink(),
                                      img);
            }
        }
    }

    return true;
}

bool VMdEditOperations::handleKeyPressEvent(QKeyEvent *p_event)
{
    if (m_editConfig->m_enableVimMode
        && m_vim->handleKeyPressEvent(p_event, &m_autoIndentPos)) {
        return true;
    }

    bool ret = false;
    int key = p_event->key();
    int modifiers = p_event->modifiers();

    switch (key) {
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    {
        if (modifiers == Qt::ControlModifier) {
            // Ctrl + <N>: insert title at level <N>.
            if (decorateHeading(key == Qt::Key_7 ? 0 : key - Qt::Key_0)) {
                p_event->accept();
                ret = true;
                goto exit;
            }
        }
        break;
    }

    case Qt::Key_Tab:
    {
        if (handleKeyTab(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_Backtab:
    {
        if (handleKeyBackTab(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_B:
    {
        if (modifiers == Qt::ControlModifier) {
            decorateBold();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_H:
    {
        if (handleKeyH(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_I:
    {
        if (modifiers == Qt::ControlModifier) {
            decorateItalic();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_L:
    {
        if (modifiers == Qt::ControlModifier) {
            m_editor->insertLink();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_Apostrophe:
    {
        if (modifiers == Qt::ControlModifier) {
            m_editor->insertImage();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_M:
    {
        if (modifiers == Qt::ControlModifier) {
            decorateCodeBlock();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_K:
    {
        if (modifiers == Qt::ControlModifier) {
            decorateInlineCode();
            p_event->accept();
            ret = true;
        }

        break;
    }

    case Qt::Key_U:
    {
        if (handleKeyU(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_W:
    {
        if (handleKeyW(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_BracketLeft:
    {
        if (handleKeyBracketLeft(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_Escape:
    {
        if (handleKeyEsc(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_Enter:
        // Fall through.
        V_FALLTHROUGH;
    case Qt::Key_Return:
    {
        if (handleKeyReturn(p_event)) {
            ret = true;
            goto exit;
        }
        break;
    }

    case Qt::Key_D:
    {
        if (modifiers == Qt::ControlModifier) {
            decorateStrikethrough();
            p_event->accept();
            ret = true;
        }

        break;
    }

    default:
        break;
    }

exit:
    // Qt::Key_Return, Qt::Key_Tab and Qt::Key_Backtab will handle m_autoIndentPos.
    if (key != Qt::Key_Return
        && key != Qt::Key_Enter
        && key != Qt::Key_Tab
        && key != Qt::Key_Backtab
        && key != Qt::Key_Shift
        && key != Qt::Key_Control) {
        m_autoIndentPos = -1;
    }

    return ret;
}

// Let Ctrl+[ behave exactly like ESC.
bool VMdEditOperations::handleKeyBracketLeft(QKeyEvent *p_event)
{
    // 1. If there is any selection, clear it.
    // 2. Otherwise, ignore this event and let parent handles it.
    if (p_event->modifiers() == Qt::ControlModifier) {
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.hasSelection()) {
            cursor.clearSelection();
            m_editor->setTextCursorW(cursor);
            p_event->accept();
            return true;
        }
    }

    return false;
}

bool VMdEditOperations::handleKeyTab(QKeyEvent *p_event)
{
    QString text(m_editConfig->m_tabSpaces);

    if (p_event->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.hasSelection()) {
            m_autoIndentPos = -1;
            cursor.beginEditBlock();
            // Indent each selected line.
            VEditUtils::indentSelectedBlocks(cursor, text, true);
            cursor.endEditBlock();
            m_editor->setTextCursorW(cursor);
        } else {
            // If it is a Tab key following auto list, increase the indent level.
            QTextBlock block = cursor.block();
            int seq = -1;
            if (m_autoIndentPos == cursor.position()
                && VEditUtils::isListBlock(block, &seq)) {
                QTextCursor blockCursor(block);
                blockCursor.beginEditBlock();
                blockCursor.insertText(text);
                if (seq != -1) {
                    changeListBlockSeqNumber(block, 1);
                }
                blockCursor.endEditBlock();
                // Change m_autoIndentPos to let it can be repeated.
                m_autoIndentPos = m_editor->textCursorW().position();
            } else {
                // Just insert "tab".
                insertTextAtCurPos(text);
                m_autoIndentPos = -1;
            }
        }
    } else {
        m_autoIndentPos = -1;
        return false;
    }
    p_event->accept();
    return true;
}

bool VMdEditOperations::handleKeyBackTab(QKeyEvent *p_event)
{
    if (p_event->modifiers() != Qt::ShiftModifier) {
        m_autoIndentPos = -1;
        return false;
    }

    QTextCursor cursor = m_editor->textCursorW();
    QTextBlock block = m_editor->documentW()->findBlock(cursor.selectionStart());
    bool continueAutoIndent = false;
    int seq = -1;
    if (cursor.position() == m_autoIndentPos
        && VEditUtils::isListBlock(block, &seq) &&
        !cursor.hasSelection()) {
        continueAutoIndent = true;
    }

    cursor.beginEditBlock();
    if (continueAutoIndent && seq != -1) {
        changeListBlockSeqNumber(block, 1);
    }

    VEditUtils::indentSelectedBlocks(cursor, m_editConfig->m_tabSpaces, false);
    cursor.endEditBlock();

    if (continueAutoIndent) {
        m_autoIndentPos = m_editor->textCursorW().position();
    } else {
        m_autoIndentPos = -1;
    }
    p_event->accept();
    return true;
}

bool VMdEditOperations::handleKeyH(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+H, equal to backspace.
        QTextCursor cursor = m_editor->textCursorW();
        cursor.deletePreviousChar();

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyU(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+U, delete till the start of line.
        QTextCursor cursor = m_editor->textCursorW();
        bool ret;
        if (cursor.atBlockStart()) {
            ret = cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        } else {
            ret = cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
        }
        if (ret) {
            cursor.removeSelectedText();
        }

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyW(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+W, delete till the start of previous word.
        QTextCursor cursor = m_editor->textCursorW();
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        } else {
            bool ret = cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
            if (ret) {
                cursor.removeSelectedText();
            }
        }
        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyEsc(QKeyEvent *p_event)
{
    // 1. If there is any selection, clear it.
    // 2. Otherwise, ignore this event and let parent handles it.
    QTextCursor cursor = m_editor->textCursorW();
    if (cursor.hasSelection()) {
        cursor.clearSelection();
        m_editor->setTextCursorW(cursor);
        p_event->accept();
        return true;
    }

    return false;
}

bool VMdEditOperations::handleKeyReturn(QKeyEvent *p_event)
{
    bool autolist = true;
    if (p_event->modifiers() & Qt::ControlModifier) {
        m_autoIndentPos = -1;
        return false;
    } else if (p_event->modifiers() & Qt::ShiftModifier) {
        // Insert two spaces and a new line.
        m_autoIndentPos = -1;

        QTextCursor cursor = m_editor->textCursorW();
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.insertText("  ");
        cursor.endEditBlock();

        // Let remaining logics handle inserting the new block except that we
        // do not need to insert auto list.
        autolist = false;
    }

    // See if we need to cancel auto indent.
    if (m_autoIndentPos > -1) {
        // Cancel the auto indent/list if the pos is the same and cursor is at
        // the end of a block.
        QTextCursor cursor = m_editor->textCursorW();
        if (VEditUtils::needToCancelAutoIndent(m_autoIndentPos, cursor)) {
            m_autoIndentPos = -1;
            VEditUtils::deleteIndentAndListMark(cursor);
            m_editor->setTextCursorW(cursor);
            return true;
        }
    }

    bool handled = false;
    m_autoIndentPos = -1;
    if (g_config->getAutoIndent()) {
        handled = true;

        QTextCursor cursor = m_editor->textCursorW();
        bool textInserted = false;
        cursor.beginEditBlock();
        cursor.removeSelectedText();

        // Indent the new line as previous line.
        textInserted = VEditUtils::insertBlockWithIndent(cursor);

        // Continue the list from previous line.
        if (g_config->getAutoList() && autolist) {
            textInserted = VEditUtils::insertListMarkAsPreviousBlock(cursor) || textInserted;
        }

        cursor.endEditBlock();
        m_editor->setTextCursorW(cursor);
        if (textInserted) {
            m_autoIndentPos = m_editor->textCursorW().position();
        }
    }

    return handled;
}

void VMdEditOperations::changeListBlockSeqNumber(QTextBlock &p_block, int p_seq)
{
    QString text = p_block.text();
    QRegExp regExp("^(\\s*)(\\d+)\\.\\s");

    int idx = regExp.indexIn(text);
    if (idx == -1 || regExp.captureCount() != 2) {
        return;
    }

    int oriSeq = -1;
    bool ok = false;
    oriSeq = regExp.capturedTexts()[2].toInt(&ok);
    if (ok && oriSeq == p_seq) {
        return;
    }

    QTextCursor cursor(p_block);
    bool ret = cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                                   regExp.capturedTexts()[1].size());
    if (!ret) {
        return;
    }

    ret = cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                              regExp.capturedTexts()[2].size());
    if (!ret) {
        return;
    }

    cursor.removeSelectedText();
    cursor.insertText(QString::number(p_seq));
}

bool VMdEditOperations::decorateHeading(int p_level)
{
    QTextDocument *doc = m_editor->documentW();
    QTextCursor cursor = m_editor->textCursorW();
    int firstBlock = cursor.block().blockNumber();
    int lastBlock = firstBlock;

    if (cursor.hasSelection()) {
        // Insert title # in front of the selected blocks.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        firstBlock = doc->findBlock(start).blockNumber();
        lastBlock = doc->findBlock(end).blockNumber();
    }

    cursor.beginEditBlock();
    for (int i = firstBlock; i <= lastBlock; ++i) {
        VEditUtils::insertTitleMark(cursor, doc->findBlockByNumber(i), p_level);
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
    return true;
}

void VMdEditOperations::decorateText(TextDecoration p_decoration, int p_level)
{
    if (p_decoration == TextDecoration::None) {
        return;
    }

    bool validDecoration = true;
    switch (p_decoration) {
    case TextDecoration::Bold:
        decorateBold();
        break;

    case TextDecoration::Italic:
        decorateItalic();
        break;

    case TextDecoration::Strikethrough:
        decorateStrikethrough();
        break;

    case TextDecoration::InlineCode:
        decorateInlineCode();
        break;

    case TextDecoration::CodeBlock:
        decorateCodeBlock();
        break;

    case TextDecoration::Heading:
        decorateHeading(p_level);
        break;

    default:
        validDecoration = false;
        qDebug() << "decoration" << (int)p_decoration << "is not implemented yet";
        break;
    }

    if (validDecoration && m_editConfig->m_enableVimMode) {
        Q_ASSERT(m_vim);
        m_vim->setMode(VimMode::Insert, false);
    }
}

void VMdEditOperations::decorateBold()
{
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Insert ** around the selected text.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.clearSelection();
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        cursor.insertText("**");
        cursor.setPosition(end + 2, QTextCursor::MoveAnchor);
        cursor.insertText("**");
    } else {
        // Insert **** and place cursor in the middle.
        // Or if there are two * after current cursor, just skip them or delete
        // them if four * appear.
        int pos = cursor.positionInBlock();
        bool hasStars = false;
        bool emptyMarkers = false;
        QString text = cursor.block().text();
        if (pos <= text.size() - 2) {
            if (text[pos] == '*' && text[pos + 1] == '*') {
                hasStars = true;

                if (pos >= 2
                    && text[pos - 1] == '*'
                    && text[pos - 2] == '*') {
                    emptyMarkers = true;
                }
            }
        }

        if (hasStars) {
            if (emptyMarkers) {
                cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, 2);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 4);
                cursor.removeSelectedText();
            } else {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
            }
        } else {
            cursor.insertText("****");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
}

void VMdEditOperations::decorateItalic()
{
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Insert * around the selected text.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.clearSelection();
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        cursor.insertText("*");
        cursor.setPosition(end + 1, QTextCursor::MoveAnchor);
        cursor.insertText("*");
    } else {
        // Insert ** and place cursor in the middle.
        // Or if there are one * after current cursor, just skip them or delete
        // them if two * appear.
        int pos = cursor.positionInBlock();
        bool hasStar = false;
        bool emptyMarkers = false;
        QString text = cursor.block().text();
        if (pos <= text.size() - 1) {
            if (text[pos] == '*'
                && (pos == text.size() - 1 || text[pos + 1] != '*')) {
                hasStar = true;

                if (pos >= 1 && text[pos - 1] == '*') {
                    emptyMarkers = true;
                }
            }
        }

        if (hasStar) {
            if (emptyMarkers) {
                cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, 1);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 2);
                cursor.removeSelectedText();
            } else {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
            }
        } else {
            cursor.insertText("**");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
}

void VMdEditOperations::decorateInlineCode()
{
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Insert ` around the selected text.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.clearSelection();
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        cursor.insertText("`");
        cursor.setPosition(end + 1, QTextCursor::MoveAnchor);
        cursor.insertText("`");
    } else {
        // Insert `` and place cursor in the middle.
        // Or if there are one ` after current cursor, just skip them or delete
        // them if two ` appear.
        int pos = cursor.positionInBlock();
        bool hasBackquote = false;
        bool emptyMarkers = false;
        QString text = cursor.block().text();
        if (pos <= text.size() - 1) {
            if (text[pos] == '`') {
                hasBackquote = true;

                if (pos >= 1 && text[pos - 1] == '`') {
                    emptyMarkers = true;
                }
            }
        }

        if (hasBackquote) {
            if (emptyMarkers) {
                cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, 1);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 2);
                cursor.removeSelectedText();
            } else {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
            }
        } else {
            cursor.insertText("``");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
}

void VMdEditOperations::decorateCodeBlock()
{
    const QString marker("```");

    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Insert ``` around the selected text.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();

        QString indentation = VEditUtils::fetchIndentSpaces(cursor.block());

        // Insert the end marker first.
        cursor.setPosition(end, QTextCursor::MoveAnchor);
        VEditUtils::insertBlock(cursor, false);
        VEditUtils::indentBlock(cursor, indentation);
        cursor.insertText(marker);

        // Insert the start marker.
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        VEditUtils::insertBlock(cursor, true);
        VEditUtils::indentBlock(cursor, indentation);
        cursor.insertText(marker);
    } else {
        // Insert ``` ``` and place cursor after the first marker.
        // Or if current block or next block is ```, we will skip it.
        QTextBlock block = cursor.block();
        int state = block.userState();
        if (state == HighlightBlockState::CodeBlock
            || state == HighlightBlockState::CodeBlockStart
            || state == HighlightBlockState::CodeBlockEnd) {
            // Find the block end.
            QTextBlock endBlock = block;
            while (endBlock.isValid()) {
                if (endBlock.userState() == HighlightBlockState::CodeBlockEnd) {
                    break;
                }

                endBlock = endBlock.next();
            }

            if (endBlock.isValid()) {
                // It is CodeBlockEnd.
                if (endBlock.previous().isValid()
                    && endBlock.previous().userState() == HighlightBlockState::CodeBlockStart) {
                    // Delete empty code blocks.
                    cursor.setPosition(endBlock.previous().position());
                    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
                    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                    cursor.removeSelectedText();
                } else {
                    cursor.setPosition(endBlock.position());
                    if (endBlock.next().isValid()) {
                        cursor.movePosition(QTextCursor::NextBlock);
                        cursor.movePosition(QTextCursor::StartOfBlock);
                    } else {
                        cursor.movePosition(QTextCursor::EndOfBlock);
                    }
                }
            } else {
                // Reach the end of the document.
                cursor.movePosition(QTextCursor::End);
            }
        } else {
            bool insertInline = false;
            if (!cursor.atBlockEnd()) {
                cursor.insertBlock();
                cursor.movePosition(QTextCursor::PreviousBlock);
            } else if (cursor.atBlockStart() || VEditUtils::isSpaceBlock(block)) {
                insertInline = true;
            }

            if (!insertInline) {
                VEditUtils::insertBlock(cursor, false);
                VEditUtils::indentBlockAsBlock(cursor, false);
            }

            cursor.insertText(marker);

            VEditUtils::insertBlock(cursor, true);
            VEditUtils::indentBlockAsBlock(cursor, true);
            cursor.insertText(marker);
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
}

void VMdEditOperations::decorateStrikethrough()
{
    QTextCursor cursor = m_editor->textCursorW();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Insert ~~ around the selected text.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        cursor.clearSelection();
        cursor.setPosition(start, QTextCursor::MoveAnchor);
        cursor.insertText("~~");
        cursor.setPosition(end + 2, QTextCursor::MoveAnchor);
        cursor.insertText("~~");
    } else {
        // Insert ~~~~ and place cursor in the middle.
        // Or if there are one ~~ after current cursor, just skip it or delete
        // it if for ~ appear.
        int pos = cursor.positionInBlock();
        bool hasStrikethrough = false;
        bool emptyMarkers = false;
        QString text = cursor.block().text();
        if (pos <= text.size() - 2) {
            if (text[pos] == '~' && text[pos + 1] == '~') {
                hasStrikethrough = true;

                if (pos >= 2
                    && text[pos - 1] == '~'
                    && text[pos - 2] == '~') {
                    emptyMarkers = true;
                }
            }
        }

        if (hasStrikethrough) {
            if (emptyMarkers) {
                cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, 2);
                cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 4);
                cursor.removeSelectedText();
            } else {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
            }
        } else {
            cursor.insertText("~~~~");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
        }
    }

    cursor.endEditBlock();
    m_editor->setTextCursorW(cursor);
}

bool VMdEditOperations::insertLink(const QString &p_linkText,
                                   const QString &p_linkUrl)
{
    QString link = QString("[%1](%2)").arg(p_linkText).arg(p_linkUrl);
    QTextCursor cursor = m_editor->textCursorW();
    cursor.insertText(link);
    m_editor->setTextCursorW(cursor);

    setVimMode(VimMode::Insert);

    return true;
}

bool VMdEditOperations::insertImageLink(const QString &p_linkText,
                                        const QString &p_linkUrl)
{
    QString link = QString("![%1](%2)").arg(p_linkText).arg(p_linkUrl);
    QTextCursor cursor = m_editor->textCursorW();
    cursor.insertText(link);
    m_editor->setTextCursorW(cursor);

    setVimMode(VimMode::Insert);

    return true;
}
