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
#include "utils/vutils.h"
#include "vedit.h"
#include "vdownloader.h"
#include "vfile.h"

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

    m_editor->insertImage(fileName);
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

    m_editor->insertImage(fileName);
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
