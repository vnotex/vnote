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
#include "utils/vutils.h"
#include "vedit.h"
#include "vdownloader.h"
#include "vfile.h"
#include "vmdedit.h"

VMdEditOperations::VMdEditOperations(VEdit *p_editor, VFile *p_file)
    : VEditOperations(p_editor, p_file)
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
    VInsertImageDialog dialog(tr("Insert image from clipboard"), tr("image_title"),
                              "", (QWidget *)m_editor);
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
    qDebug() << "insert image" << path << title << fileName;
    QString filePath = QDir(path).filePath(fileName);
    Q_ASSERT(!QFile(filePath).exists());
    VUtils::makeDirectory(path);
    bool ret = image.save(filePath);
    if (!ret) {
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Fail to save image %1").arg(filePath),
                           QMessageBox::Ok, (QWidget *)m_editor);
        msgBox.exec();
        return;
    }

    QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
    insertTextAtCurPos(md);

    VMdEdit *mdEditor = dynamic_cast<VMdEdit *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(fileName);
}

void VMdEditOperations::insertImageFromPath(const QString &title,
                                            const QString &path, const QString &oriImagePath)
{
    QString fileName = VUtils::generateImageFileName(path, title, QFileInfo(oriImagePath).suffix());
    qDebug() << "insert image" << path << title << fileName << oriImagePath;
    QString filePath = QDir(path).filePath(fileName);
    Q_ASSERT(!QFile(filePath).exists());
    VUtils::makeDirectory(path);
    bool ret = QFile::copy(oriImagePath, filePath);
    if (!ret) {
        qWarning() << "error: fail to copy" << oriImagePath << "to" << filePath;
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Fail to save image %1").arg(filePath),
                           QMessageBox::Ok, (QWidget *)m_editor);
        msgBox.exec();
        return;
    }

    QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
    insertTextAtCurPos(md);

    VMdEdit *mdEditor = dynamic_cast<VMdEdit *>(m_editor);
    Q_ASSERT(mdEditor);
    mdEditor->imageInserted(fileName);
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
            qWarning() << "error: image is null";
            return false;
        }
        title = "Insert image from file";
    } else {
        imagePath = imageUrl.toString();
        title = "Insert image from network";
    }


    VInsertImageDialog dialog(title, QObject::tr("image_title"), imagePath, (QWidget *)m_editor);
    dialog.setBrowseable(false);
    if (isLocal) {
        dialog.setImage(image);
    } else {
        // Download it to a QImage
        VDownloader *downloader = new VDownloader(&dialog);
        QObject::connect(downloader, &VDownloader::downloadFinished,
                         &dialog, &VInsertImageDialog::imageDownloaded);
        downloader->download(imageUrl.toString());
    }
    if (dialog.exec() == QDialog::Accepted) {
        if (isLocal) {
            insertImageFromPath(dialog.getImageTitleInput(), m_file->retriveImagePath(),
                                imagePath);
        } else {
            insertImageFromQImage(dialog.getImageTitleInput(), m_file->retriveImagePath(),
                                dialog.getImage());
        }
    }
    return true;
}

bool VMdEditOperations::insertURLFromMimeData(const QMimeData *source)
{
    foreach (QUrl url, source->urls()) {
        QString urlStr;
        if (url.isLocalFile()) {
            urlStr = url.toLocalFile();
        } else {
            urlStr = url.toString();
        }
        QFileInfo info(urlStr);
        if (QImageReader::supportedImageFormats().contains(info.suffix().toLower().toLatin1())) {
            insertImageFromURL(url);
        } else {
            insertTextAtCurPos(urlStr);
        }
    }
    return true;
}

bool VMdEditOperations::insertImage()
{
    VInsertImageDialog dialog(tr("Insert Image From File"), tr("image_title"),
                              "", (QWidget *)m_editor);
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
    } else {
        if (modifiers == (Qt::ControlModifier | Qt::AltModifier)) {
            switch (key) {
            // Should add one item for each supported Ctrl+ALT+<Key> Vim binding.
            case Qt::Key_H:
            case Qt::Key_J:
            case Qt::Key_K:
            case Qt::Key_L:
            case Qt::Key_0:
            case Qt::Key_1:
            case Qt::Key_2:
            case Qt::Key_3:
            case Qt::Key_4:
            case Qt::Key_5:
            case Qt::Key_6:
            case Qt::Key_7:
            case Qt::Key_8:
            case Qt::Key_9:
            case Qt::Key_X:
            case Qt::Key_W:
            case Qt::Key_E:
            case Qt::Key_B:
            case Qt::Key_G:
            case Qt::Key_V:
            case Qt::Key_Y:
            case Qt::Key_Dollar:
            case Qt::Key_AsciiCircum:
            {
                return true;
            }
            default:
                m_pendingKey.clear();
                break;
            }
        } else if (modifiers ==
                   (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) {
            switch (key) {
            case Qt::Key_G:
            case Qt::Key_Dollar:
            case Qt::Key_AsciiCircum:
            {
                return true;
            }

            default:
                // Should not clear the m_pendingKey.
                // Consider Ctrl+Shift+Alt+G.
                break;
            }
        }
    }
    return false;
}

bool VMdEditOperations::handleKeyPressEvent(QKeyEvent *p_event)
{
    if (shouldTriggerVimMode(p_event)) {
        if (handleKeyPressVim(p_event)) {
            return true;
        }
    } else {
        switch (p_event->key()) {
        case Qt::Key_Tab:
        {
            if (handleKeyTab(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_Backtab:
        {
            if (handleKeyBackTab(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_B:
        {
            if (handleKeyB(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_D:
        {
            if (handleKeyD(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_H:
        {
            if (handleKeyH(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_I:
        {
            if (handleKeyI(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_U:
        {
            if (handleKeyU(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_W:
        {
            if (handleKeyW(p_event)) {
                return true;
            }
            break;
        }

        case Qt::Key_BracketLeft:
        {
            if (p_event->modifiers() != Qt::ControlModifier) {
                break;
            }
            // Ctrl+[, Fall through.
        }
        case Qt::Key_Escape:
        {
            if (handleKeyEsc(p_event)) {
                return true;
            }
            break;
        }

        default:
            break;
        }
    }

    return false;
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
        cursor.beginEditBlock();
        if (cursor.hasSelection()) {
            // Indent each selected line.
            QTextBlock block = doc->findBlock(cursor.selectionStart());
            QTextBlock endBlock = doc->findBlock(cursor.selectionEnd());
            int endBlockNum = endBlock.blockNumber();
            while (true) {
                Q_ASSERT(block.isValid());
                QTextCursor blockCursor(block);
                blockCursor.insertText(text);

                if (block.blockNumber() == endBlockNum) {
                    break;
                }
                block = block.next();
            }
        } else {
            // Just insert "tab".
            insertTextAtCurPos(text);
        }
        cursor.endEditBlock();
    } else {
        return false;
    }
    p_event->accept();
    return true;
}

bool VMdEditOperations::handleKeyBackTab(QKeyEvent *p_event)
{
    if (p_event->modifiers() != Qt::ShiftModifier) {
        return false;
    }
    QTextDocument *doc = m_editor->document();
    QTextCursor cursor = m_editor->textCursor();
    QTextBlock block = doc->findBlock(cursor.selectionStart());
    QTextBlock endBlock = doc->findBlock(cursor.selectionEnd());
    int endBlockNum = endBlock.blockNumber();
    cursor.beginEditBlock();
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
        m_keyState = KeyState::Vim;
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
        bool ret = cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        if (ret) {
            cursor.removeSelectedText();
        }

        p_event->accept();
        return true;
    }
    return false;
}

bool VMdEditOperations::handleKeyEsc(QKeyEvent *p_event)
{
    // Esc, clear any Vim mode, clear selection.
    QTextCursor cursor = m_editor->textCursor();
    cursor.clearSelection();
    m_editor->setTextCursor(cursor);

    m_pendingTimer->stop();
    m_keyState = KeyState::Normal;
    m_pendingKey.clear();

    p_event->accept();
    return true;
}

bool VMdEditOperations::handleKeyPressVim(QKeyEvent *p_event)
{
    int modifiers = p_event->modifiers();
    bool ctrlAlt = modifiers == (Qt::ControlModifier | Qt::AltModifier);
    bool ctrlShiftAlt = modifiers == (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
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
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            QTextCursor::MoveOperation op;
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
            QTextCursor cursor = m_editor->textCursor();
            cursor.movePosition(op, mode, repeat == 0 ? 1 : repeat);
            m_editor->setTextCursor(cursor);
            if (visualMode) {
                goto pending;
            }
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
        if (modifiers == Qt::NoModifier || ctrlAlt) {
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
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            int repeat = keySeqToNumber(m_pendingKey);
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.beginEditBlock();
            for (int i = 0; i < repeat; ++i) {
                cursor.deleteChar();
            }
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        }
        break;
    }

    case Qt::Key_W:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            // Move to the start of the next word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
            QTextCursor cursor = m_editor->textCursor();
            if (repeat == 0) {
                repeat = 1;
            }
            cursor.movePosition(QTextCursor::NextWord, mode, repeat);
            m_editor->setTextCursor(cursor);
            if (visualMode) {
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_E:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            // Move to the end of the next word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
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
            if (visualMode) {
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_B:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            // Move to the start of the previous word.
            // Slightly different from the Vim behavior.
            int repeat = keySeqToNumber(m_pendingKey);
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
            if (visualMode) {
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_0:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            if (keySeqToNumber(m_pendingKey) == 0) {
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::StartOfLine, mode);
                m_editor->setTextCursor(cursor);
                if (visualMode) {
                    goto pending;
                }
            } else {
                m_pendingKey.append("0");
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_Dollar:
    {
        if (modifiers == Qt::ShiftModifier || ctrlShiftAlt) {
            if (m_pendingKey.isEmpty()) {
                // Go to end of line.
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::EndOfLine, mode);
                m_editor->setTextCursor(cursor);
                if (visualMode) {
                    goto pending;
                }
            }
        }
        break;
    }

    case Qt::Key_AsciiCircum:
    {
        if (modifiers == Qt::ShiftModifier || ctrlShiftAlt) {
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
                if (visualMode) {
                    goto pending;
                }
            }
        }
        break;
    }

    case Qt::Key_G:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            // g, pending or go to first line.
            if (m_pendingKey.isEmpty()) {
                m_pendingKey.append("g");
                goto pending;
            } else if (m_pendingKey.size() == 1 && m_pendingKey.at(0) == "g") {
                m_pendingKey.clear();
                QTextCursor cursor = m_editor->textCursor();
                cursor.movePosition(QTextCursor::Start, mode);
                m_editor->setTextCursor(cursor);
                if (visualMode) {
                    goto pending;
                }
            }
        } else if (modifiers == Qt::ShiftModifier || ctrlShiftAlt) {
            // G, go to a certain line or the end of document.
            int lineNum = keySeqToNumber(m_pendingKey);
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
            if (visualMode) {
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_V:
    {
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            // V to enter visual mode.
            if (m_pendingKey.isEmpty() && m_keyState != KeyState::VimVisual) {
                m_keyState = KeyState::VimVisual;
                goto pending;
            }
        }
        break;
    }

    case Qt::Key_Y:
    {
        // TODO: support y2j.
        if (modifiers == Qt::NoModifier || ctrlAlt) {
            if (m_pendingKey.isEmpty()) {
                QTextCursor cursor = m_editor->textCursor();
                if (cursor.hasSelection()) {
                    QString text = cursor.selectedText();
                    QClipboard *clipboard = QApplication::clipboard();
                    clipboard->setText(text);
                }
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
    m_keyState = KeyState::Normal;
    m_pendingKey.clear();
    p_event->accept();
    return true;

pending:
    // When pending in Ctrl+Alt, we just want to clear m_pendingKey.
    if (m_pendingTimer->isActive() || ctrlAlt) {
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
    int modifiers = QGuiApplication::keyboardModifiers();
    if (modifiers == (Qt::ControlModifier | Qt::AltModifier)
        || m_keyState == KeyState::VimVisual) {
        m_pendingTimer->start();
        return;
    }
    m_keyState = KeyState::Normal;
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

