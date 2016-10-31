#include <QtDebug>
#include <QImage>
#include <QVariant>
#include <QMimeData>
#include <QObject>
#include <QWidget>
#include <QImageReader>
#include <QDir>
#include <QMessageBox>
#include "vmdeditoperations.h"
#include "dialog/vinsertimagedialog.h"
#include "vnotefile.h"
#include "utils/vutils.h"
#include "vedit.h"

VMdEditOperations::VMdEditOperations(VEdit *editor, VNoteFile *noteFile)
    : VEditOperations(editor, noteFile)
{
}

bool VMdEditOperations::insertImageFromMimeData(const QMimeData *source)
{
    QImage image = qvariant_cast<QImage>(source->imageData());
    if (image.isNull()) {
        return false;
    }
    VInsertImageDialog dialog(QObject::tr("Insert image from clipboard"), QObject::tr("image_title"),
                              "", (QWidget *)editor);
    dialog.setBrowseable(false);
    dialog.setImage(image);
    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.getImageTitleInput();
        QString path = QDir::cleanPath(QDir(noteFile->basePath).filePath("images"));
        QString fileName = VUtils::generateImageFileName(path, title);
        qDebug() << "insert image" << path << title << fileName;
        QString filePath = QDir(path).filePath(fileName);
        Q_ASSERT(!QFile(filePath).exists());
        bool ret = image.save(filePath);
        if (!ret) {
            QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
                               QMessageBox::Ok, (QWidget *)editor);
            msgBox.exec();
            return true;
        }

        QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
        insertTextAtCurPos(md);
    }
    return true;
}

bool VMdEditOperations::insertImageFromPath(const QString &imagePath)
{
    QImage image(imagePath);
    if (image.isNull()) {
        qWarning() << "error: image is null";
        return false;
    }
    VInsertImageDialog dialog(QObject::tr("Insert image from file"), QObject::tr("image_title"),
                              "", (QWidget *)editor);
    dialog.setBrowseable(false);
    dialog.setImage(image);
    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.getImageTitleInput();
        QString path = QDir::cleanPath(QDir(noteFile->basePath).filePath("images"));
        QString fileName = VUtils::generateImageFileName(path, title, QFileInfo(imagePath).suffix());
        qDebug() << "insert image" << path << title << fileName;
        QString filePath = QDir(path).filePath(fileName);
        Q_ASSERT(!QFile(filePath).exists());
        bool ret = QFile::copy(imagePath, filePath);
        if (!ret) {
            qWarning() << "error: fail to copy" << imagePath << "to" << filePath;
            QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
                               QMessageBox::Ok, (QWidget *)editor);
            msgBox.exec();
            return false;
        }

        QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
        insertTextAtCurPos(md);
    }
    return true;
}

bool VMdEditOperations::insertURLFromMimeData(const QMimeData *source)
{
    foreach (QUrl url, source->urls()) {
        if (url.isLocalFile()) {
            QFileInfo info(url.toLocalFile());
            if (QImageReader::supportedImageFormats().contains(info.suffix().toLower().toLatin1())) {
                insertImageFromPath(info.filePath());
            } else {
                insertTextAtCurPos(url.toLocalFile());
            }
        } else {
            // TODO: download http image
            // Just insert the URL for non-image
            insertTextAtCurPos(url.toString());
        }
    }
    return true;
}
