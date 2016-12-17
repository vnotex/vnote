#include <QtDebug>
#include <QImage>
#include <QVariant>
#include <QMimeData>
#include <QObject>
#include <QWidget>
#include <QImageReader>
#include <QDir>
#include <QMessageBox>
#include <QKeyEvent>
#include <QTextCursor>
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
}

bool VMdEditOperations::insertImageFromMimeData(const QMimeData *source)
{
    QImage image = qvariant_cast<QImage>(source->imageData());
    if (image.isNull()) {
        return false;
    }
    VInsertImageDialog dialog(QObject::tr("Insert image from clipboard"), QObject::tr("image_title"),
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
        QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
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
        QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
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
    VInsertImageDialog dialog(QObject::tr("Insert Image From File"), QObject::tr("image_title"),
                              "", (QWidget *)m_editor);
    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.getImageTitleInput();
        QString imagePath = dialog.getPathInput();
        qDebug() << "insert image from" << imagePath << "as" << title;
        insertImageFromPath(title, m_file->retriveImagePath(), imagePath);
    }
    return true;
}

bool VMdEditOperations::handleKeyPressEvent(QKeyEvent *p_event)
{
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

    case Qt::Key_H:
    {
        if (handleKeyH(p_event)) {
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

    case Qt::Key_U:
    {
        if (handleKeyU(p_event)) {
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

    case Qt::Key_I:
    {
        if (handleKeyI(p_event)) {
            return true;
        }
        break;
    }

    default:
        // Fall through.
        break;
    }

    m_keyState = KeyState::Normal;
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
            cursor.beginEditBlock();
            cursor.insertText("****");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);
            cursor.endEditBlock();
            m_editor->setTextCursor(cursor);
        }

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
            cursor.beginEditBlock();
            cursor.insertText("**");
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
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

