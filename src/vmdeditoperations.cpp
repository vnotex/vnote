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
#include "vedit.h"
#include "vdownloader.h"
#include "vfile.h"
#include "vmdedit.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

const QString VMdEditOperations::c_defaultImageTitle = "image";

VMdEditOperations::VMdEditOperations(VEdit *p_editor, VFile *p_file)
    : VEditOperations(p_editor, p_file), m_autoIndentPos(-1)
{
    m_pendingTimer = new QTimer(this);
    m_pendingTimer->setSingleShot(true);
    m_pendingTimer->setInterval(m_pendingTime * 1000);  // milliseconds
    connect(m_pendingTimer, &QTimer::timeout, this, &VMdEditOperations::pendingTimerTimeout);
}

bool VMdEditOperations::insertImageFromMimeData(const QMimeData *source)
{
    QImage image = qvariant_cast<QImage>(source->imageData());
    if (image.isNull()) {
        return false;
    }
    VInsertImageDialog dialog(tr("Insert Image From Clipboard"),
                              c_defaultImageTitle, "", (QWidget *)m_editor);
    dialog.setBrowseable(false);
    dialog.setImage(image);
    if (dialog.exec() == QDialog::Accepted) {
        insertImageFromQImage(dialog.getImageTitleInput(), m_file->retriveImagePath(), image);
    }
    return true;
}

void VMdEditOperations::insertImageFromQImage(const QString &title, const QString &path,
                                              const QImage &image)
{
    QString fileName = VUtils::generateImageFileName(path, title);
    QString filePath = QDir(path).filePath(fileName);
    V_ASSERT(!QFile(filePath).exists());

    QString errStr;
    bool ret = VUtils::makePath(path);
    if (!ret) {
        errStr = tr("Fail to create image folder <span style=\"%1\">%2</span>.")
                   .arg(vconfig.c_dataTextStyle).arg(path);
    } else {
        ret = image.save(filePath);
        if (!ret) {
            errStr = tr("Fail to save image <span style=\"%1\">%2</span>.")
                       .arg(vconfig.c_dataTextStyle).arg(filePath);
        }
    }

    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to insert image <span style=\"%1\">%2</span>.").arg(vconfig.c_dataTextStyle).arg(title),
                            errStr,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            (QWidget *)m_editor);
        return;
    }

    QString md = QString("![%1](%2/%3)").arg(title).arg(VUtils::directoryNameFromPath(path)).arg(fileName);
    insertTextAtCurPos(md);

    qDebug() << "insert image" << title << filePath;

    VMdEdit *mdEditor = dynamic_cast<VMdEdit *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(filePath);
}

void VMdEditOperations::insertImageFromPath(const QString &title,
                                            const QString &path, const QString &oriImagePath)
{
    QString fileName = VUtils::generateImageFileName(path, title, QFileInfo(oriImagePath).suffix());
    QString filePath = QDir(path).filePath(fileName);
    V_ASSERT(!QFile(filePath).exists());

    QString errStr;
    bool ret = VUtils::makePath(path);
    if (!ret) {
        errStr = tr("Fail to create image folder <span style=\"%1\">%2</span>.")
                   .arg(vconfig.c_dataTextStyle).arg(path);
    } else {
        ret = QFile::copy(oriImagePath, filePath);
        if (!ret) {
            errStr = tr("Fail to copy image <span style=\"%1\">%2</span>.")
                       .arg(vconfig.c_dataTextStyle).arg(filePath);
        }
    }

    if (!ret) {
        VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                            tr("Fail to insert image <span style=\"%1\">%2</span>.").arg(vconfig.c_dataTextStyle).arg(title),
                            errStr,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            (QWidget *)m_editor);
        return;
    }

    QString md = QString("![%1](%2/%3)").arg(title).arg(VUtils::directoryNameFromPath(path)).arg(fileName);
    insertTextAtCurPos(md);

    qDebug() << "insert image" << title << filePath;

    VMdEdit *mdEditor = dynamic_cast<VMdEdit *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(filePath);
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
        image = QImage(imagePath);

        if (image.isNull()) {
            qWarning() << "image is null";
            return false;
        }
        title = "Insert Image From File";
    } else {
        imagePath = imageUrl.toString();
        title = "Insert Image From Network";
    }


    VInsertImageDialog dialog(title, c_defaultImageTitle,
                              imagePath, (QWidget *)m_editor);
    dialog.setBrowseable(false, true);
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
                                m_file->retriveImagePath(),
                                imagePath);
        } else {
            insertImageFromQImage(dialog.getImageTitleInput(),
                                  m_file->retriveImagePath(),
                                  dialog.getImage());
        }
    }
    return true;
}

bool VMdEditOperations::insertImage()
{
    VInsertImageDialog dialog(tr("Insert Image From File"),
                              c_defaultImageTitle, "", (QWidget *)m_editor);
    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.getImageTitleInput();
        QString imagePath = dialog.getPathInput();
        qDebug() << "insert image from" << imagePath << "as" << title;
        insertImageFromPath(title, m_file->retriveImagePath(), imagePath);
    }
    return true;
}

// Will modify m_pendingKey.
bool VMdEditOperations::shouldTriggerVimMode(QKeyEvent *p_event)
{
    int modifiers = p_event->modifiers();
    int key = p_event->key();
    if (key == Qt::Key_Escape ||
        (key == Qt::Key_BracketLeft && modifiers == Qt::ControlModifier)) {
        return false;
    } else if (m_keyState == KeyState::Vim || m_keyState == KeyState::VimVisual) {
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyPressEvent(QKeyEvent *p_event)
{
    bool ret = false;
    int key = p_event->key();
    int modifiers = p_event->modifiers();

    if (shouldTriggerVimMode(p_event)) {
        if (handleKeyPressVim(p_event)) {
            ret = true;
            goto exit;
        }
    } else {
        switch (key) {
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        {
            if (modifiers == Qt::ControlModifier) {
                // Ctrl + <N>: insert title at level <N>.
                if (insertTitle(key - Qt::Key_0)) {
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
            if (handleKeyB(p_event)) {
                ret = true;
                goto exit;
            }
            break;
        }

        case Qt::Key_D:
        {
            if (handleKeyD(p_event)) {
                ret = true;
                goto exit;
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
            if (handleKeyI(p_event)) {
                ret = true;
                goto exit;
            }
            break;
        }

        case Qt::Key_O:
        {
            if (handleKeyO(p_event)) {
                ret = true;
                goto exit;
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

        case Qt::Key_Return:
        {
            if (handleKeyReturn(p_event)) {
                ret = true;
                goto exit;
            }
            break;
        }

        default:
            break;
        }
    }

exit:
    // Qt::Key_Return, Qt::Key_Tab and Qt::Key_Backtab will handle m_autoIndentPos.
    if (key != Qt::Key_Return && key != Qt::Key_Tab && key != Qt::Key_Backtab &&
        key != Qt::Key_Shift) {
        m_autoIndentPos = -1;
    }
    return ret;
}

// Let Ctrl+[ behave exactly like ESC.
bool VMdEditOperations::handleKeyBracketLeft(QKeyEvent *p_event)
{
    // 1. If it is not in Normal state, just go back to Normal state;
    // 2. If it is already Normal state, try to clear selection;
    // 3. Otherwise, ignore this event and let parent handles it.
    bool accept = false;
    if (p_event->modifiers() == Qt::ControlModifier) {
        if (m_keyState != KeyState::Normal) {
            m_pendingTimer->stop();
            setKeyState(KeyState::Normal);
            m_pendingKey.clear();
            accept = true;
        } else {
            QTextCursor cursor = m_editor->textCursor();
            if (cursor.hasSelection()) {
                cursor.clearSelection();
                m_editor->setTextCursor(cursor);
                accept = true;
            }
        }
    }
    if (accept) {
        p_event->accept();
    }
    return accept;
}

bool VMdEditOperations::handleKeyTab(QKeyEvent *p_event)
{
    QTextDocument *doc = m_editor->document();
    QString text("\t");
    if (m_expandTab) {
        text = m_tabSpaces;
    }

    if (p_event->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) {
            m_autoIndentPos = -1;
            cursor.beginEditBlock();
            // Indent each selected line.
            QTextBlock block = doc->findBlock(cursor.selectionStart());
            QTextBlock endBlock = doc->findBlock(cursor.selectionEnd());
            int endBlockNum = endBlock.blockNumber();
            while (true) {
                Q_ASSERT(block.isValid());
                if (!block.text().isEmpty()) {
                    QTextCursor blockCursor(block);
                    blockCursor.insertText(text);
                }

                if (block.blockNumber() == endBlockNum) {
                    break;
                }

                block = block.next();
            }
            cursor.endEditBlock();
        } else {
            // If it is a Tab key following auto list, increase the indent level.
            QTextBlock block = cursor.block();
            int seq = -1;
            if (m_autoIndentPos == cursor.position() && isListBlock(block, &seq)) {
                QTextCursor blockCursor(block);
                blockCursor.beginEditBlock();
                blockCursor.insertText(text);
                if (seq != -1) {
                    changeListBlockSeqNumber(block, 1);
                }
                blockCursor.endEditBlock();
                // Change m_autoIndentPos to let it can be repeated.
                m_autoIndentPos = m_editor->textCursor().position();
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
    QTextDocument *doc = m_editor->document();
    QTextCursor cursor = m_editor->textCursor();
    QTextBlock block = doc->findBlock(cursor.selectionStart());
    QTextBlock endBlock = doc->findBlock(cursor.selectionEnd());

    bool continueAutoIndent = false;
    int seq = -1;
    if (cursor.position() == m_autoIndentPos && isListBlock(block, &seq) &&
        !cursor.hasSelection()) {
        continueAutoIndent = true;
    }
    int endBlockNum = endBlock.blockNumber();
    cursor.beginEditBlock();
    if (continueAutoIndent && seq != -1) {
        changeListBlockSeqNumber(block, 1);
    }

    for (; block.isValid() && block.blockNumber() <= endBlockNum;
         block = block.next()) {
        QTextCursor blockCursor(block);
        QString text = block.text();
        if (text.isEmpty()) {
            continue;
        } else if (text[0] == '\t') {
            blockCursor.deleteChar();
            continue;
        } else if (text[0] != ' ') {
            continue;
        } else {
            // Spaces.
            if (m_expandTab) {
                int width = m_tabSpaces.size();
                for (int i = 0; i < width; ++i) {
                    if (text[i] == ' ') {
                        blockCursor.deleteChar();
                    } else {
                        break;
                    }
                }
                continue;
            } else {
                blockCursor.deleteChar();
                continue;
            }
        }
    }
    cursor.endEditBlock();

    if (continueAutoIndent) {
        m_autoIndentPos = m_editor->textCursor().position();
    } else {
        m_autoIndentPos = -1;
    }
    p_event->accept();
    return true;
}

bool VMdEditOperations::handleKeyB(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+B, Bold.
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) {
            // Insert ** around the selected text.
            int start = cursor.selectionStart();
            int end = cursor.selectionEnd();
            cursor.beginEditBlock();
            cursor.clearSelection();
            cursor.setPosition(start, QTextCursor::MoveAnchor);
            cursor.insertText("**");
            cursor.setPosition(end + 2, QTextCursor::MoveAnchor);
            cursor.insertText("**");
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        } else {
            // Insert **** and place cursor in the middle.
            // Or if there are two * after current cursor, just skip them.
            cursor.beginEditBlock();
            int pos = cursor.positionInBlock();
            bool hasStars = false;
            QString text = cursor.block().text();
            if (pos <= text.size() - 2) {
                if (text[pos] == '*' && text[pos + 1] == '*') {
                    hasStars = true;
                }
            }
            if (hasStars) {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
            } else {
                cursor.insertText("****");
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        }

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyD(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+D, enter Vim-pending mode.
        // Will accept the key stroke in m_pendingTime as Vim normal command.
        setKeyState(KeyState::Vim);
        m_pendingTimer->stop();
        m_pendingTimer->start();
        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyH(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+H, equal to backspace.
        QTextCursor cursor = m_editor->textCursor();
        cursor.deletePreviousChar();

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyI(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+I, Italic.
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) {
            // Insert * around the selected text.
            int start = cursor.selectionStart();
            int end = cursor.selectionEnd();
            cursor.beginEditBlock();
            cursor.clearSelection();
            cursor.setPosition(start, QTextCursor::MoveAnchor);
            cursor.insertText("*");
            cursor.setPosition(end + 1, QTextCursor::MoveAnchor);
            cursor.insertText("*");
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        } else {
            // Insert ** and place cursor in the middle.
            // Or if there are one * after current cursor, just skip it.
            cursor.beginEditBlock();
            int pos = cursor.positionInBlock();
            bool hasStar = false;
            QString text = cursor.block().text();
            if (pos <= text.size() - 1) {
                if (text[pos] == '*') {
                    hasStar = true;
                }
            }
            if (hasStar) {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
            } else {
                cursor.insertText("**");
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        }

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyO(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+O, inline codeblock.
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) {
            // Insert ` around the selected text.
            int start = cursor.selectionStart();
            int end = cursor.selectionEnd();
            cursor.beginEditBlock();
            cursor.clearSelection();
            cursor.setPosition(start, QTextCursor::MoveAnchor);
            cursor.insertText("`");
            cursor.setPosition(end + 1, QTextCursor::MoveAnchor);
            cursor.insertText("`");
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        } else {
            // Insert `` and place cursor in the middle.
            // Or if there are one ` after current cursor, just skip it.
            cursor.beginEditBlock();
            int pos = cursor.positionInBlock();
            bool hasBackquote = false;
            QString text = cursor.block().text();
            if (pos <= text.size() - 1) {
                if (text[pos] == '`') {
                    hasBackquote = true;
                }
            }
            if (hasBackquote) {
                cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
            } else {
                cursor.insertText("``");
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        }
        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyU(QKeyEvent *p_event)
{
    if (p_event->modifiers() == Qt::ControlModifier) {
        // Ctrl+U, delete till the start of line.
        QTextCursor cursor = m_editor->textCursor();
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
        QTextCursor cursor = m_editor->textCursor();
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
    // 1. If it is not in Normal state, just go back to Normal state;
    // 2. If it is already Normal state, try to clear selection;
    // 3. Otherwise, ignore this event and let parent handles it.
    bool accept = false;
    if (m_keyState != KeyState::Normal) {
        m_pendingTimer->stop();
        setKeyState(KeyState::Normal);
        m_pendingKey.clear();
        accept = true;
    } else {
        QTextCursor cursor = m_editor->textCursor();
        if (cursor.hasSelection()) {
            cursor.clearSelection();
            m_editor->setTextCursor(cursor);
            accept = true;
        }
    }
    if (accept) {
        p_event->accept();
    }
    return accept;
}

bool VMdEditOperations::handleKeyReturn(QKeyEvent *p_event)
{
    if (p_event->modifiers() & Qt::ControlModifier) {
        m_autoIndentPos = -1;
        return false;
    }

    // See if we need to cancel auto indent.
    if (m_autoIndentPos > -1) {
        // Cancel the auto indent/list if the pos is the same and cursor is at
        // the end of a block.
        bool cancelAutoIndent = false;
        QTextCursor cursor = m_editor->textCursor();
        QTextBlock block = cursor.block();
        if (cursor.position() == m_autoIndentPos && !cursor.hasSelection()) {
            if (isListBlock(block)) {
                if (cursor.atBlockEnd()) {
                    cancelAutoIndent = true;
                }
            } else if (isSpaceToBlockStart(block, cursor.positionInBlock())) {
                cancelAutoIndent = true;
            }
        }
        if (cancelAutoIndent) {
            m_autoIndentPos = -1;
            deleteIndentAndListMark();
            return true;
        }
    }

    bool handled = false;
    m_autoIndentPos = -1;
    if (vconfig.getAutoIndent()) {
        handled = true;

        bool textInserted = false;
        // Indent the new line as previous line.
        textInserted = insertNewBlockWithIndent();

        // Continue the list from previous line.
        if (vconfig.getAutoList()) {
            textInserted = insertListMarkAsPreviousLine() || textInserted;
        }
        if (textInserted) {
            m_autoIndentPos = m_editor->textCursor().position();
        }
    }
    return handled;
}

bool VMdEditOperations::insertNewBlockWithIndent()
{
    QTextCursor cursor = m_editor->textCursor();
    bool ret = false;

    cursor.beginEditBlock();
    cursor.removeSelectedText();
    QTextBlock block = cursor.block();
    QString text = block.text();
    QRegExp regExp("(^\\s*)");
    regExp.indexIn(text);
    V_ASSERT(regExp.captureCount() == 1);
    QString leadingSpaces = regExp.capturedTexts()[1];
    cursor.insertBlock();
    if (!leadingSpaces.isEmpty()) {
        cursor.insertText(leadingSpaces);
        ret = true;
    }
    cursor.endEditBlock();
    m_editor->setTextCursor(cursor);
    return ret;
}

bool VMdEditOperations::insertListMarkAsPreviousLine()
{
    bool ret = false;
    QTextCursor cursor = m_editor->textCursor();
    QTextBlock block = cursor.block();
    QTextBlock preBlock = block.previous();
    QString text = preBlock.text();
    QRegExp regExp("^\\s*(-|\\d+\\.)\\s");
    int regIdx = regExp.indexIn(text);
    if (regIdx != -1) {
        ret = true;
        V_ASSERT(regExp.captureCount() == 1);
        cursor.beginEditBlock();
        QString markText = regExp.capturedTexts()[1];
        if (markText == "-") {
            // Insert - in front.
            cursor.insertText("- ");
        } else {
            // markText is like "123.".
            V_ASSERT(markText.endsWith('.'));
            bool ok = false;
            int num = markText.left(markText.size() - 1).toInt(&ok, 10);
            V_ASSERT(ok);
            num++;
            cursor.insertText(QString::number(num, 10) + ". ");
        }
        cursor.endEditBlock();
        m_editor->setTextCursor(cursor);
    }
    return ret;
}

bool VMdEditOperations::isListBlock(const QTextBlock &p_block, int *p_seq)
{
    QString text = p_block.text();
    QRegExp regExp("^\\s*(-|\\d+\\.)\\s");

    if (p_seq) {
        *p_seq = -1;
    }

    int regIdx = regExp.indexIn(text);
    if (regIdx == -1) {
        return false;
    }

    V_ASSERT(regExp.captureCount() == 1);
    QString markText = regExp.capturedTexts()[1];
    if (markText != "-") {
        V_ASSERT(markText.endsWith('.'));
        bool ok = false;
        int num = markText.left(markText.size() - 1).toInt(&ok, 10);
        V_ASSERT(ok);
        if (p_seq) {
            *p_seq = num;
        }
    }

    return true;
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

bool VMdEditOperations::isSpaceToBlockStart(const QTextBlock &p_block, int p_posInBlock)
{
    if (p_posInBlock <= 0) {
        return true;
    }
    QString text = p_block.text();
    V_ASSERT(text.size() >= p_posInBlock);
    return text.left(p_posInBlock).trimmed().isEmpty();
}

void VMdEditOperations::deleteIndentAndListMark()
{
    QTextCursor cursor = m_editor->textCursor();
    V_ASSERT(!cursor.hasSelection());
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_editor->setTextCursor(cursor);
}

bool VMdEditOperations::handleKeyPressVim(QKeyEvent *p_event)
{
    int modifiers = p_event->modifiers();
    bool visualMode = m_keyState == KeyState::VimVisual;
    QTextCursor::MoveMode mode = visualMode ? QTextCursor::KeepAnchor
                                              : QTextCursor::MoveAnchor;

    switch (p_event->key()) {
    // Ctrl and Shift may be sent out first.
    case Qt::Key_Control:
        // Fall through.
    case Qt::Key_Shift:
    {
        goto pending;
        break;
    }

    case Qt::Key_H:
    case Qt::Key_J:
    case Qt::Key_K:
    case Qt::Key_L:
    {
        if (modifiers == Qt::NoModifier) {
            QTextCursor::MoveOperation op = QTextCursor::Left;
            switch (p_event->key()) {
            case Qt::Key_H:
                op = QTextCursor::Left;
                break;
            case Qt::Key_J:
                op = QTextCursor::Down;
                break;
            case Qt::Key_K:
                op = QTextCursor::Up;
                break;
            case Qt::Key_L:
                op = QTextCursor::Right;
            }
            // Move cursor <repeat> characters left/Down/Up/Right.
            int repeat = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            cursor.movePosition(op, mode, repeat == 0 ? 1 : repeat);
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
    {
        if (modifiers == Qt::NoModifier) {
            if (!suffixNumAllowed(m_pendingKey)) {
                break;
            }
            int num = p_event->key() - Qt::Key_0;
            m_pendingKey.append(QString::number(num));
            goto pending;
        }
        break;
    }

    case Qt::Key_X:
    {
        // Delete characters.
        if (modifiers == Qt::NoModifier) {
            int repeat = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.beginEditBlock();
            if (cursor.hasSelection()) {
                QClipboard *clipboard = QApplication::clipboard();
                clipboard->setText(cursor.selectedText());
                cursor.removeSelectedText();
            } else {
                for (int i = 0; i < repeat; ++i) {
                    cursor.deleteChar();
                }
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_W:
    {
        if (modifiers == Qt::NoModifier) {
            // Move to the start of the next word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.movePosition(QTextCursor::NextWord, mode, repeat);
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_E:
    {
        if (modifiers == Qt::NoModifier) {
            // Move to the end of the next word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.beginEditBlock();
            int pos = cursor.position();
            // First move to the end of current word.
            cursor.movePosition(QTextCursor::EndOfWord, mode);
            if (cursor.position() != pos) {
                // We did move.
                repeat--;
            }
            if (repeat) {
                cursor.movePosition(QTextCursor::NextWord, mode, repeat);
                cursor.movePosition(QTextCursor::EndOfWord, mode);
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_B:
    {
        if (modifiers == Qt::NoModifier) {
            // Move to the start of the previous word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.beginEditBlock();
            int pos = cursor.position();
            // First move to the start of current word.
            cursor.movePosition(QTextCursor::StartOfWord, mode);
            if (cursor.position() != pos) {
                // We did move.
                repeat--;
            }
            if (repeat) {
                cursor.movePosition(QTextCursor::PreviousWord, mode, repeat);
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_0:
    {
        if (modifiers == Qt::NoModifier) {
            if (keySeqToNumber(m_pendingKey) == 0) {
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::StartOfLine, mode);
                m_editor->setTextCursor(cursor);
            } else {
                m_pendingKey.append("0");
            }
            goto pending;
        }
        break;
    }

    case Qt::Key_Dollar:
    {
        if (modifiers == Qt::ShiftModifier) {
            if (m_pendingKey.isEmpty()) {
                // Go to end of line.
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::EndOfLine, mode);
                m_editor->setTextCursor(cursor);
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_AsciiCircum:
    {
        if (modifiers == Qt::ShiftModifier) {
            if (m_pendingKey.isEmpty()) {
                // Go to first non-space character of current line.
                QTextCursor cursor = m_editor->textCursor();
                QTextBlock block = cursor.block();
                QString text = block.text();
                cursor.beginEditBlock();
                if (text.trimmed().isEmpty()) {
                    cursor.movePosition(QTextCursor::StartOfLine, mode);
                } else {
                    cursor.movePosition(QTextCursor::StartOfLine, mode);
                    int pos = cursor.positionInBlock();
                    while (pos < text.size() && text[pos].isSpace()) {
                        cursor.movePosition(QTextCursor::NextWord, mode);
                        pos = cursor.positionInBlock();
                    }
                }
                cursor.endEditBlock();
                m_editor->setTextCursor(cursor);
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_G:
    {
        if (modifiers == Qt::NoModifier) {
            // g, pending or go to first line.
            if (m_pendingKey.isEmpty()) {
                m_pendingKey.append("g");
                goto pending;
            } else if (m_pendingKey.size() == 1 && m_pendingKey.at(0) == "g") {
                m_pendingKey.clear();
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::Start, mode);
                m_editor->setTextCursor(cursor);
                goto pending;
            }
        } else if (modifiers == Qt::ShiftModifier) {
            // G, go to a certain line or the end of document.
            int lineNum = keySeqToNumber(m_pendingKey);
            m_pendingKey.clear();
            QTextCursor cursor = m_editor->textCursor();
            if (lineNum == 0) {
                cursor.movePosition(QTextCursor::End, mode);
            } else {
                QTextDocument *doc = m_editor->document();
                QTextBlock block = doc->findBlockByNumber(lineNum - 1);
                if (block.isValid()) {
                    cursor.setPosition(block.position(), mode);
                } else {
                    // Go beyond the document.
                    cursor.movePosition(QTextCursor::End, mode);
                }
            }
            m_editor->setTextCursor(cursor);
            goto pending;
        }
        break;
    }

    case Qt::Key_V:
    {
        if (modifiers == Qt::NoModifier) {
            // V to enter visual mode.
            if (m_pendingKey.isEmpty() && m_keyState != KeyState::VimVisual) {
                setKeyState(KeyState::VimVisual);
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_Y:
    {
        if (modifiers == Qt::NoModifier) {
            if (m_pendingKey.isEmpty()) {
                QTextCursor cursor = m_editor->textCursor();
                if (cursor.hasSelection()) {
                    QString text = cursor.selectedText();
                    QClipboard *clipboard = QApplication::clipboard();
                    clipboard->setText(text);
                }
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_D:
    {
        if (modifiers == Qt::NoModifier) {
            // d, pending or delete current line.
            QTextCursor cursor = m_editor->textCursor();
            if (m_pendingKey.isEmpty()) {
                if (cursor.hasSelection()) {
                    cursor.deleteChar();
                    m_editor->setTextCursor(cursor);
                } else {
                    m_pendingKey.append("d");
                }
                goto pending;
            } else if (m_pendingKey.size() == 1 && m_pendingKey.at(0) == "d") {
                m_pendingKey.clear();
                cursor.select(QTextCursor::BlockUnderCursor);
                cursor.removeSelectedText();
                m_editor->setTextCursor(cursor);
                goto pending;
            }
        }
        break;
    }

    default:
        // Unknown key. End Vim mode.
        break;
    }

    m_pendingTimer->stop();
    if (m_keyState == KeyState::VimVisual) {
        // Clear the visual selection.
        QTextCursor cursor = m_editor->textCursor();
        cursor.clearSelection();
        m_editor->setTextCursor(cursor);
    }
    setKeyState(KeyState::Normal);
    m_pendingKey.clear();
    p_event->accept();
    return true;

pending:
    // When pending in Ctrl+Alt, we just want to clear m_pendingKey.
    if (m_pendingTimer->isActive()) {
        m_pendingTimer->stop();
        m_pendingTimer->start();
    }
    p_event->accept();
    return true;
}

int VMdEditOperations::keySeqToNumber(const QList<QString> &p_seq)
{
    int num = 0;
    for (int i = 0; i < p_seq.size(); ++i) {
        QString tmp = p_seq.at(i);
        bool ok;
        int tmpInt = tmp.toInt(&ok);
        if (!ok) {
            return 0;
        }
        num = num * 10 + tmpInt;
    }
    return num;
}

void VMdEditOperations::pendingTimerTimeout()
{
    qDebug() << "key pending timer timeout";
    if (m_keyState == KeyState::VimVisual) {
        m_pendingTimer->start();
        return;
    }
    setKeyState(KeyState::Normal);
    m_pendingKey.clear();
}

bool VMdEditOperations::suffixNumAllowed(const QList<QString> &p_seq)
{
    if (!p_seq.isEmpty()) {
        QString firstEle = p_seq.at(0);
        if (firstEle[0].isDigit()) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void VMdEditOperations::setKeyState(KeyState p_state)
{
    if (m_keyState == p_state) {
        return;
    }
    m_keyState = p_state;
    emit keyStateChanged(m_keyState);
}

bool VMdEditOperations::insertTitle(int p_level)
{
    Q_ASSERT(p_level > 0 && p_level < 7);
    QTextDocument *doc = m_editor->document();
    QString titleMark(p_level, '#');
    QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        // Insert title # in front of the selected lines.
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        int startBlock = doc->findBlock(start).blockNumber();
        int endBlock = doc->findBlock(end).blockNumber();
        cursor.beginEditBlock();
        cursor.clearSelection();
        for (int i = startBlock; i <= endBlock; ++i) {
            QTextBlock block = doc->findBlockByNumber(i);
            cursor.setPosition(block.position(), QTextCursor::MoveAnchor);
            cursor.insertText(titleMark + " ");
        }
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.endEditBlock();
    } else {
        // Insert title # in front of current block.
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.insertText(titleMark + " ");
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.endEditBlock();
    }
    m_editor->setTextCursor(cursor);
    return true;
}

