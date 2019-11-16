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
#include <QCryptographicHash>
#include <QRegExp>

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
                                   const QString &p_imagePath,
                                   const QString &p_token);

    // Parameters needed to generate uploaded images.
    QString githubImageBedGenerateParam(const QString p_imagePath);

    // Control image to upload.
    void githubImageBedUploadManager();

    // Replace old links with new ones for images.
    void githubImageBedReplaceLink(QString p_fileContent, const QString p_filePath);

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
    VFile *m_file;
};

class VGiteeImageHosting : public QObject
{
    Q_OBJECT
public:
    explicit VGiteeImageHosting(VFile *p_file, QWidget *p_parent = nullptr);

    // GitHub identity authentication.
    void authenticateGiteeImageHosting(const QString &p_username,
                                       const QString &p_repository,
                                       const QString &p_token);

    // Upload a single image.
    void giteeImageBedUploadImage(const QString &p_username,
                                  const QString &p_repository,
                                  const QString &p_imagePath,
                                  const QString &p_token);

    // Parameters needed to generate uploaded images.
    QString giteeImageBedGenerateParam(const QString &p_imagePath, const QString &p_token);

    // Control image to upload.
    void giteeImageBedUploadManager();

    // Replace old links with new ones for images.
    void giteeImageBedReplaceLink(QString p_fileContent, const QString p_filePath);

    // Process the image upload request to Gitee.
    void handleUploadImageToGiteeRequested();

public slots:
    // Gitee image hosting identity authentication completed.
    void giteeImageBedAuthFinished();

    // Gitee image hosting upload completed.
    void giteeImageBedUploadFinished();

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
    void wechatImageBedReplaceLink(QString &p_fileContent, const QString &p_filePath);

    // Upload a single image.
    void wechatImageBedUploadImage(const QString p_imagePath, const QString p_token);

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

class VTencentImageHosting : public QObject
{
    Q_OBJECT
public:
    explicit VTencentImageHosting(VFile *p_file, QWidget *p_parent = nullptr);

    QByteArray hmacSha1(const QByteArray &p_key, const QByteArray &p_baseString);

    QString getAuthorizationString(const QString &p_secretId,
                                   const QString &p_secretKey,
                                   const QString &p_imgName);

    void findAndStartUploadImage();

    QByteArray getImgContent(const QString &p_imagePath);

    // Control image to upload.
    void tencentImageBedUploadManager();

    // Replace old links with new ones for images.
    void tencentImageBedReplaceLink(QString &p_fileContent, const QString &p_filePath);

    // Upload a single image.
    void tencentImageBedUploadImage(const QString &p_imagePath,
                                    const QString &p_accessDomainName,
                                    const QString &p_secretId,
                                    const QString &p_secretKey);

    // Process image upload request to tencent.
    void handleUploadImageToTencentRequested();

public slots:
    // Tencent image hosting upload completed.
    void tencentImageBedUploadFinished();

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
    QString new_file_name;
    VFile *m_file;
};
#endif // VGITHUBIMAGEHOSTING_H
