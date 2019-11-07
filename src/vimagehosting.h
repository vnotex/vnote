#ifndef VGITHUBIMAGEHOSTING_H
#define VGITHUBIMAGEHOSTING_H

#include <QObject>
#include <QtNetwork>
#include <QProgressDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QApplication>
#include <vfile.h>
#include <QClipboard>

class VGithubImageHosting : public QObject
{
    Q_OBJECT
public:
    explicit VGithubImageHosting(VFile *p_file, QWidget *p_parent = nullptr);

    // GitHub identity authentication.
    void authenticateGithubImageHosting(QString p_token);

    // Upload a single image.
    void githubImageBedUploadImage(const QString &p_username,
                                   const QString &p_repository,
                                   const QString &p_image_path,
                                   const QString &p_token);

    // Parameters needed to generate uploaded images.
    QString githubImageBedGenerateParam(const QString p_image_path);

    // Control image to upload.
    void githubImageBedUploadManager();

    // Replace old links with new ones for images.
    void githubImageBedReplaceLink(const QString p_file_content, const QString p_file_path);

    // Process the image upload request to GitHub.
    void handleUploadImageToGithubRequested();

public slots:
    // GitHub image hosting identity authentication completed.
    void githubImageBedAuthFinished();

    // GitHub image hosting upload completed.
    void githubImageBedUploadFinished();

private:
    QNetworkAccessManager manager;
    QNetworkReply *reply;
    QMap<QString, QString> imageUrlMap;
    // Similar to "_v_image/".
    QString imageBasePath;
    // Replace the file content with the new link.
    QString newFileContent;
    // Whether the picture has been uploaded successfully.
    bool imageUploaded;
    // Image upload progress bar.
    QProgressDialog *proDlg;
    // Total number of images to upload.
    int uploadImageCount;
    int uploadImageCountIndex;
    // Currently uploaded picture name.
    QString currentUploadImage;
    // Image upload status.
    bool uploadImageStatus;
    // Token returned after successful wechat authentication.
    QString wechatAccessToken;
    // Relative image path currently Uploaded.
    QString currentUploadRelativeImagePah;
    VFile *m_file;
};

class VWechatImageHosting : public QObject
{
    Q_OBJECT
public:
    explicit VWechatImageHosting(VFile *p_file, QWidget *p_parent = nullptr);

    // Wechat identity authentication.
    void authenticateWechatImageHosting(const QString p_appid, const QString p_secret);

    // Control image to upload.
    void wechatImageBedUploadManager();

    // Replace old links with new ones for images.
    void wechatImageBedReplaceLink(const QString p_file_content);

    // Upload a single image.
    void wechatImageBedUploadImage(const QString p_image_path, const QString p_token);

    // Process image upload request to wechat.
    void handleUploadImageToWechatRequested();

public slots:
    // Wechat mage hosting identity authentication completed.
    void wechatImageBedAuthFinished();

    // Wechat image hosting upload completed.
    void wechatImageBedUploadFinished();

private:
    QNetworkAccessManager manager;
    QNetworkReply *reply;
    QMap<QString, QString> imageUrlMap;
    // Similar to "_v_image/".
    QString imageBasePath;
    // Replace the file content with the new link.
    QString newFileContent;
    // Whether the picture has been uploaded successfully.
    bool imageUploaded;
    // Image upload progress bar.
    QProgressDialog *proDlg;
    // Total number of images to upload.
    int uploadImageCount;
    int uploadImageCountIndex;
    // Currently uploaded picture name.
    QString currentUploadImage;
    // Image upload status
    bool uploadImageStatus;
    // Token returned after successful wechat authentication.
    QString wechatAccessToken;
    // Relative image path currently Uploaded.
    QString currentUploadRelativeImagePah;
    VFile *m_file;
};

#endif // VGITHUBIMAGEHOSTING_H
