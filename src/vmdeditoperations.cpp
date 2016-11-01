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
#include "vdownloader.h"

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
        insertImageFromQImage(dialog.getImageTitleInput(),
                              QDir::cleanPath(QDir(noteFile->basePath).filePath("images")),
                              image);
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
    bool ret = image.save(filePath);
    if (!ret) {
        QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
                           QMessageBox::Ok, (QWidget *)editor);
        msgBox.exec();
        return;
    }

    QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
    insertTextAtCurPos(md);
}

void VMdEditOperations::insertImageFromPath(const QString &title,
                                            const QString &path, const QString &oriImagePath)
{
    QString fileName = VUtils::generateImageFileName(path, title, QFileInfo(oriImagePath).suffix());
    qDebug() << "insert image" << path << title << fileName << oriImagePath;
    QString filePath = QDir(path).filePath(fileName);
    Q_ASSERT(!QFile(filePath).exists());
    bool ret = QFile::copy(oriImagePath, filePath);
    if (!ret) {
        qWarning() << "error: fail to copy" << oriImagePath << "to" << filePath;
        QMessageBox msgBox(QMessageBox::Warning, QObject::tr("Warning"), QString("Fail to save image %1").arg(filePath),
                           QMessageBox::Ok, (QWidget *)editor);
        msgBox.exec();
        return;
    }

    QString md = QString("![%1](images/%2)").arg(title).arg(fileName);
    insertTextAtCurPos(md);
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


    VInsertImageDialog dialog(title, QObject::tr("image_title"), imagePath, (QWidget *)editor);
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
            insertImageFromPath(dialog.getImageTitleInput(),
                                QDir::cleanPath(QDir(noteFile->basePath).filePath("images")),
                                imagePath);
        } else {
            insertImageFromQImage(dialog.getImageTitleInput(),
                                QDir::cleanPath(QDir(noteFile->basePath).filePath("images")),
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
