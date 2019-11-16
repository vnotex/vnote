#include "vimagehosting.h"
#include "utils/vutils.h"
#include "vedittab.h"

extern VConfigManager *g_config;

VGithubImageHosting::VGithubImageHosting(VFile *p_file, QWidget *p_parent)
    :QObject(p_parent),
     m_file(p_file)
{
    reply = Q_NULLPTR;
    imageUploaded = false;
}

void VGithubImageHosting::handleUploadImageToGithubRequested()
{
    qDebug() << "Start processing the image upload request to GitHub";

    if(g_config->getGithubPersonalAccessToken().isEmpty() ||
       g_config->getGithubReposName().isEmpty() ||
       g_config->getGithubUserName().isEmpty())
    {
        qDebug() << "Please configure the GitHub image hosting first!";
        QMessageBox::warning(nullptr,
                             tr("Github Image Hosting"),
                             tr("Please configure the GitHub image hosting first!"));
        return;
    }

    authenticateGithubImageHosting(g_config->getGithubPersonalAccessToken());
}

void VGithubImageHosting::authenticateGithubImageHosting(QString p_token)
{
    qDebug() << "start the authentication process ";
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QNetworkRequest request;
    QUrl url = QUrl("https://api.github.com");
    QString ptoken = "token " + p_token;
    request.setRawHeader("Authorization", ptoken.toLocal8Bit());
    request.setUrl(url);
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }
    reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &VGithubImageHosting::githubImageBedAuthFinished);
}

void VGithubImageHosting::githubImageBedAuthFinished()
{
    switch (reply->error()) {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();

        if(bytes.contains("Bad credentials"))
        {
            qDebug() << "Authentication failed";
            QApplication::restoreOverrideCursor();  // Recovery pointer
            QMessageBox::warning(nullptr, tr("Github Image Hosting"), tr("Bad credentials!! ") +
                                 tr("Please check your Github Image Hosting parameters !!"));
            return;
        }
        else
        {
            qDebug() << "Authentication completed";

            qDebug() << "The current article path is: " << m_file->fetchPath();
            imageBasePath = m_file->fetchBasePath();
            newFileContent = m_file->getContent();

            QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,ImageLink::LocalRelativeInternal);
            QApplication::restoreOverrideCursor();  // Recovery pointer
            if(images.size() > 0)
            {
                proDlg = new QProgressDialog(tr("Uploading images to github..."),
                                             tr("Abort"),
                                             0,
                                             images.size(),
                                             nullptr);
                proDlg->setWindowModality(Qt::WindowModal);
                proDlg->setWindowTitle(tr("Uploading Images To Github"));
                proDlg->setMinimumDuration(1);

                uploadImageCount = images.size();
                uploadImageCountIndex  = uploadImageCount;
                for(int i=0;i<images.size() ;i++)
                {
                    if(images[i].m_url.contains(".png") ||
                       images[i].m_url.contains(".jpg") ||
                       images[i].m_url.contains(".gif"))
                    {
                        imageUrlMap.insert(images[i].m_url, "");
                    }
                    else
                    {
                        delete proDlg;
                        imageUrlMap.clear();
                        qDebug() << "Unsupported type...";
                        QFileInfo fileInfo(images[i].m_path.toLocal8Bit());
                        QString fileSuffix = fileInfo.suffix();
                        QString info = tr("Unsupported type: ") + fileSuffix;
                        QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
                        return;
                    }
                }
                githubImageBedUploadManager();
            }
            else
            {
                qDebug() << m_file->getName() << " No images to upload";
                QString info = m_file->getName() + " No pictures to upload";
                QMessageBox::information(nullptr, tr("Github Image Hosting"), info);
            }
        }
        break;
    }
    default:
    {
        QApplication::restoreOverrideCursor();  // Recovery pointer
        qDebug() << "Network error: " << reply->errorString() << " error " << reply->error();
        QString info = tr("Network error: ") + reply->errorString() + tr("\n\nPlease check your network!");
        QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
    }
    }
}

void VGithubImageHosting::githubImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString imageToUpload;
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == "")
        {
            imageToUpload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(imageToUpload));
            break;
        }
    }

    if(imageToUpload == "")
    {
        qDebug() << "All images have been uploaded";
        githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    if(g_config->getGithubPersonalAccessToken().isEmpty() ||
       g_config->getGithubReposName().isEmpty() ||
       g_config->getGithubUserName().isEmpty())
    {
        qDebug() << "Please configure the GitHub image hosting first!";
        QMessageBox::warning(nullptr, tr("Github Image Hosting"), tr("Please configure the GitHub image hosting first!"));
        imageUrlMap.clear();
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += imageToUpload;
    githubImageBedUploadImage(g_config->getGithubUserName(),
                              g_config->getGithubReposName(),
                              path,
                              g_config->getGithubPersonalAccessToken());
}

void VGithubImageHosting::githubImageBedUploadImage(const QString &p_username,
                                                    const QString &p_repository,
                                                    const QString &p_imagePath,
                                                    const QString &p_token)
{
    QFileInfo fileInfo(p_imagePath.toLocal8Bit());
    if(!fileInfo.exists())
    {
        qDebug() << "The picture does not exist in this path: " << p_imagePath.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + p_imagePath.toLocal8Bit();
        QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QString fileSuffix = fileInfo.suffix();  // file extension
    QString fileName = fileInfo.fileName();  // filename
    QString uploadUrl;  // Image upload URL
    uploadUrl = "https://api.github.com/repos/" +
                p_username +
                "/" +
                p_repository +
                "/contents/" +
                QString::number(QDateTime::currentDateTime().toTime_t()) +
                "_" +
                fileName;
    if(fileSuffix != QString::fromLocal8Bit("jpg") &&
       fileSuffix != QString::fromLocal8Bit("png") &&
       fileSuffix != QString::fromLocal8Bit("gif"))
    {
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + fileSuffix;
        QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QNetworkRequest request;
    QUrl url = QUrl(uploadUrl);
    QString ptoken = "token " + p_token;
    request.setRawHeader("Authorization", ptoken.toLocal8Bit());
    request.setUrl(url);
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }

    QString param = githubImageBedGenerateParam(p_imagePath);
    QByteArray postData;
    postData.append(param);
    reply = manager.put(request, postData);
    qDebug() << "Start uploading images: " + p_imagePath + " Waiting for upload to complete";
    uploadImageStatus = true;
    currentUploadImage = p_imagePath;
    connect(reply, &QNetworkReply::finished, this, &VGithubImageHosting::githubImageBedUploadFinished);
}

void VGithubImageHosting::githubImageBedUploadFinished()
{
    if (proDlg->wasCanceled())
    {
        qDebug() << "User stops uploading";
        reply->abort();        // Stop network request
        imageUrlMap.clear();
        // The ones that have been uploaded successfully before still need to stay
        if(imageUploaded)
        {
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(httpStatus == 201)
        {
            qDebug() <<  "Upload success";

            QString downloadUrl;
            QString imageName;
            QJsonDocument doucment = QJsonDocument::fromJson(bytes);
            if (!doucment.isNull())
            {
                if (doucment.isObject())
                {
                    QJsonObject object = doucment.object();
                    if (object.contains("content"))
                    {
                        QJsonValue value = object.value("content");
                        if (value.isObject())
                        {
                            QJsonObject obj = value.toObject();
                            if (obj.contains("download_url"))
                            {
                                QJsonValue value = obj.value("download_url");
                                if (value.isString())
                                {
                                    downloadUrl = value.toString();
                                    qDebug() << "json decode: download_url : " << downloadUrl;
                                    imageUploaded = true;  // On behalf of successfully uploaded images
                                    proDlg->setValue(uploadImageCount);
                                }
                            }
                            if(obj.contains("name"))
                            {
                                QJsonValue value = obj.value("name");
                                if(value.isString())
                                {
                                    imageName = value.toString();
                                }
                            }

                            // Traverse key in imageurlmap
                            QList<QString> klist =  imageUrlMap.keys();
                            QString temp;
                            for(int i=0;i<klist.count();i++)
                            {
                                temp = klist[i].split("/")[1];
                                if(imageName.contains(temp))
                                {
                                    // You can assign values in the map
                                    imageUrlMap.insert(klist[i], downloadUrl);

                                    // Replace the link in the original
                                    newFileContent.replace(klist[i], downloadUrl);

                                    break;
                                }
                            }
                            // Start calling the method.
                            // Whether the value in the map is empty determines whether to stop.
                            githubImageBedUploadManager();
                        }
                    }
                }
            }
            else
            {
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Resolution failure!";
                qDebug() << "Resolution failure's json: " << bytes;
                if(imageUploaded)
                {
                    githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
                }
                QString info = tr("Json decode error, Please contact the developer~");
                QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
            }
        }
        else
        {
            // If status is not 201, it means there is a problem.
            delete proDlg;
            imageUrlMap.clear();
            qDebug() << "Upload failure";
            if(imageUploaded)
            {
                githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
            }
            QString info = tr("github status code != 201, Please contact the developer~");
            QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
        }
        break;
    }
    default:
    {
        delete proDlg;
        imageUrlMap.clear();
        qDebug()<<"network error: " << reply->errorString() << " error " << reply->error();
        QByteArray bytes = reply->readAll();
        qDebug() << bytes;
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "status: " << httpStatus;

        if(imageUploaded)
        {
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        QString info = tr("Uploading ") +
                       currentUploadImage +
                       tr(" \n\nNetwork error: ") +
                       reply->errorString() +
                       tr("\n\nPlease check the network or image size");
        QMessageBox::warning(nullptr, tr("Github Image Hosting"), info);
    }
    }
}

void VGithubImageHosting::githubImageBedReplaceLink(QString p_fileContent, const QString p_filePath)
{
    // This function must be executed when the upload is completed or fails in the middle.
    if(!g_config->getGithubKeepImgScale())
    {
        // delete image scale
        p_fileContent.replace(QRegExp("\\s+=\\d+x"),"");
    }

    if(!g_config->getGithubDoNotReplaceLink())
    {
        // Write content to file.
        QFile file(p_filePath);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        file.write(p_fileContent.toUtf8());
        file.close();
    }

    // Write content to clipboard.
    QClipboard *board = QApplication::clipboard();
    board->setText(p_fileContent);
    QMessageBox::warning(nullptr,
                         tr("Github Image Hosting"),
                         tr("The article has been copied to the clipboard!"));

    // Reset.
    imageUrlMap.clear();
    imageUploaded = false;
}

QString VGithubImageHosting::githubImageBedGenerateParam(const QString p_imagePath)
{
    // According to the requirements of GitHub interface, pictures must be in Base64 format.
    // Image to base64.
    QByteArray hexed;
    QFile imgFile(p_imagePath);
    imgFile.open(QIODevice::ReadOnly);
    hexed = imgFile.readAll().toBase64();

    QString imgBase64 = hexed;
    QJsonObject json;
    json.insert("message", QString("updatetest"));
    json.insert("content", imgBase64);

    QJsonDocument document;
    document.setObject(json);
    QByteArray byteArray = document.toJson(QJsonDocument::Compact);
    QString jsonStr(byteArray);
    return jsonStr;
}

VGiteeImageHosting::VGiteeImageHosting(VFile *p_file, QWidget *p_parent)
    :QObject(p_parent),
     m_file(p_file)
{
    reply = Q_NULLPTR;
    imageUploaded = false;
}

void VGiteeImageHosting::handleUploadImageToGiteeRequested()
{
    qDebug() << "Start processing the image upload request to Gitee";

    if(g_config->getGiteePersonalAccessToken().isEmpty() ||
       g_config->getGiteeReposName().isEmpty() ||
       g_config->getGiteeUserName().isEmpty())
    {
        qDebug() << "Please configure the Gitee image hosting first!";
        QMessageBox::warning(nullptr,
                             tr("Gitee Image Hosting"),
                             tr("Please configure the Gitee image hosting first!"));
        return;
    }

    authenticateGiteeImageHosting(g_config->getGiteeUserName(),
                                  g_config->getGiteeReposName(),
                                  g_config->getGiteePersonalAccessToken());
}

void VGiteeImageHosting::authenticateGiteeImageHosting(const QString &p_username,
                                                       const QString &p_repository,
                                                       const QString &p_token)
{
    qDebug() << "start the authentication process ";
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QNetworkRequest request;
    QString url = "https://gitee.com/api/v5/repos/";
    url += p_username + "/";
    url += p_repository + "/branches/master?access_token=";
    url += p_token;
    qDebug() << "gitee url: " << url;
    QUrl qurl = QUrl(url);
    request.setUrl(qurl);
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }
    reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &VGiteeImageHosting::giteeImageBedAuthFinished);
}

void VGiteeImageHosting::giteeImageBedAuthFinished()
{
    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();

        qDebug() << "Authentication completed";

        qDebug() << "The current article path is: " << m_file->fetchPath();
        imageBasePath = m_file->fetchBasePath();
        newFileContent = m_file->getContent();

        QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file, ImageLink::LocalRelativeInternal);
        QApplication::restoreOverrideCursor();  // Recovery pointer
        if(images.size() > 0)
        {
            proDlg = new QProgressDialog(tr("Uploading images to gitee..."),
                                         tr("Abort"),
                                         0,
                                         images.size(),
                                         nullptr);
            proDlg->setWindowModality(Qt::WindowModal);
            proDlg->setWindowTitle(tr("Uploading Images To Gitee"));
            proDlg->setMinimumDuration(1);

            uploadImageCount = images.size();
            uploadImageCountIndex  = uploadImageCount;
            for(int i=0;i<images.size() ;i++)
            {
                if(images[i].m_url.contains(".png") ||
                   images[i].m_url.contains(".jpg") ||
                   images[i].m_url.contains(".gif"))
                {
                    imageUrlMap.insert(images[i].m_url, "");
                }
                else
                {
                    delete proDlg;
                    imageUrlMap.clear();
                    qDebug() << "Unsupported type...";
                    QFileInfo fileInfo(images[i].m_path.toLocal8Bit());
                    QString fileSuffix = fileInfo.suffix();
                    QString info = tr("Unsupported type: ") + fileSuffix;
                    QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
                    return;
                }
            }
            giteeImageBedUploadManager();
        }
        else
        {
            qDebug() << m_file->getName() << " No images to upload";
            QString info = m_file->getName() + " No pictures to upload";
            QMessageBox::information(nullptr, tr("Gitee Image Hosting"), info);
        }
        break;
    }
    default:
    {
        QApplication::restoreOverrideCursor();  // Recovery pointer
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(httpStatus == 401 || httpStatus == 404)
        {
            qDebug() << "Authentication failed: " << reply->errorString() << " error " << reply->error();
            QString info = tr("Authentication failed: ") +
                           reply->errorString() +
                           tr("\n\nPlease check your Gitee Image Hosting parameters !!");
            QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
        }
        else
        {
            qDebug() << "Network error: " << reply->errorString() << " error " << reply->error();
            QString info = tr("Network error: ") + reply->errorString() + tr("\n\nPlease check your network!");
            QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
        }
    }
    }
}

void VGiteeImageHosting::giteeImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString imageToUpload;
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == "")
        {
            imageToUpload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(imageToUpload));
            break;
        }
    }

    if(imageToUpload == "")
    {
        qDebug() << "All images have been uploaded";
        giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    if(g_config->getGiteePersonalAccessToken().isEmpty() ||
       g_config->getGiteeReposName().isEmpty() ||
       g_config->getGiteeUserName().isEmpty())
    {
        qDebug() << "Please configure the Gitee image hosting first!";
        QMessageBox::warning(nullptr,
                             tr("Gitee Image Hosting"),
                             tr("Please configure the Gitee image hosting first!"));
        imageUrlMap.clear();
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += imageToUpload;
    giteeImageBedUploadImage(g_config->getGiteeUserName(),
                             g_config->getGiteeReposName(),
                             path,
                             g_config->getGiteePersonalAccessToken());
}

void VGiteeImageHosting::giteeImageBedUploadImage(const QString &p_username,
                                                  const QString &p_repository,
                                                  const QString &p_imagePath,
                                                  const QString &p_token)
{
    QFileInfo fileInfo(p_imagePath.toLocal8Bit());
    if(!fileInfo.exists())
    {
        qDebug() << "The picture does not exist in this path: " << p_imagePath.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + p_imagePath.toLocal8Bit();
        QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QString fileSuffix = fileInfo.suffix();  // file extension
    QString fileName = fileInfo.fileName();  // filename
    QString uploadUrl;  // Image upload URL
    uploadUrl = "https://gitee.com/api/v5/repos/" +
                p_username +
                "/" +
                p_repository +
                "/contents/" +
                QString::number(QDateTime::currentDateTime().toTime_t()) +
                "_" +
                fileName;
    if(fileSuffix != QString::fromLocal8Bit("jpg") &&
       fileSuffix != QString::fromLocal8Bit("png") &&
       fileSuffix != QString::fromLocal8Bit("gif"))
    {
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + fileSuffix;
        QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QNetworkRequest request;
    QUrl url = QUrl(uploadUrl);
    request.setUrl(url);
    request.setRawHeader("Content-Type", "application/json;charset=UTF-8");
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }

    QString param = giteeImageBedGenerateParam(p_imagePath, p_token);
    QByteArray postData;
    postData.append(param);
    reply = manager.post(request, postData);
    qDebug() << "Start uploading images: " + p_imagePath + " Waiting for upload to complete";
    uploadImageStatus = true;
    currentUploadImage = p_imagePath;
    connect(reply, &QNetworkReply::finished, this, &VGiteeImageHosting::giteeImageBedUploadFinished);
}

void VGiteeImageHosting::giteeImageBedUploadFinished()
{
    if (proDlg->wasCanceled())
    {
        qDebug() << "User stops uploading";
        reply->abort();        // Stop network request
        imageUrlMap.clear();
        // The ones that have been uploaded successfully before still need to stay
        if(imageUploaded)
        {
            giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(httpStatus == 201)
        {
            qDebug() <<  "Upload success";

            QString downloadUrl;
            QString imageName;
            QJsonDocument doucment = QJsonDocument::fromJson(bytes);
            if (!doucment.isNull())
            {
                if (doucment.isObject())
                {
                    QJsonObject object = doucment.object();
                    if (object.contains("content"))
                    {
                        QJsonValue value = object.value("content");
                        if (value.isObject()) {
                            QJsonObject obj = value.toObject();
                            if (obj.contains("download_url"))
                            {
                                QJsonValue value = obj.value("download_url");
                                if (value.isString())
                                {
                                    downloadUrl = value.toString();
                                    qDebug() << "json decode: download_url : " << downloadUrl;
                                    imageUploaded = true;  // On behalf of successfully uploaded images
                                    proDlg->setValue(uploadImageCount);
                                }
                            }
                            if(obj.contains("name"))
                            {
                                QJsonValue value = obj.value("name");
                                if(value.isString())
                                {
                                    imageName = value.toString();
                                }
                            }

                            // Traverse key in imageurlmap
                            QList<QString> klist =  imageUrlMap.keys();
                            QString temp;
                            for(int i=0;i<klist.count();i++)
                            {
                                temp = klist[i].split("/")[1];
                                if(imageName.contains(temp))
                                {
                                    // You can assign values in the map
                                    imageUrlMap.insert(klist[i], downloadUrl);

                                    // Replace the link in the original
                                    newFileContent.replace(klist[i], downloadUrl);

                                    break;
                                }
                            }
                            // Start calling the method.
                            // Whether the value in the map is empty determines whether to stop.
                            giteeImageBedUploadManager();
                        }
                    }
                }
            }
            else
            {
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Resolution failure!";
                qDebug() << "Resolution failure's json: " << bytes;
                if(imageUploaded)
                {
                    giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
                }
                QString info = tr("Json decode error, Please contact the developer~");
                QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
            }
        }else{
            // If status is not 201, it means there is a problem.
            delete proDlg;
            imageUrlMap.clear();
            qDebug() << "Upload failure";
            if(imageUploaded)
            {
                giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
            }
            QString info = tr("gitee status code != 201, Please contact the developer~");
            QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
        }
        break;
    }
    default:
    {
        delete proDlg;
        imageUrlMap.clear();
        qDebug()<<"network error: " << reply->errorString() << " error " << reply->error();
        QByteArray bytes = reply->readAll();
        qDebug() << bytes;
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "status: " << httpStatus;

        if(imageUploaded)
        {
            giteeImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        QString info = tr("Uploading ") +
                       currentUploadImage +
                       tr(" \n\nNetwork error: ") +
                       reply->errorString() +
                       tr("\n\nPlease check the network or image size");
        QMessageBox::warning(nullptr, tr("Gitee Image Hosting"), info);
    }
    }
}

void VGiteeImageHosting::giteeImageBedReplaceLink(QString p_fileContent, const QString p_file_path)
{
    // This function must be executed when the upload is completed or fails in the middle.
    if(!g_config->getGiteeKeepImgScale())
    {
        // delete image scale
        p_fileContent.replace(QRegExp("\\s+=\\d+x"),"");
    }

    if(!g_config->getGiteeDoNotReplaceLink())
    {
        // Write content to file.
        QFile file(p_file_path);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        file.write(p_fileContent.toUtf8());
        file.close();
    }

    // Write content to clipboard.
    QClipboard *board = QApplication::clipboard();
    board->setText(p_fileContent);
    QMessageBox::warning(nullptr,
                         tr("Gitee Image Hosting"),
                         tr("The article has been copied to the clipboard!"));

    // Reset.
    imageUrlMap.clear();
    imageUploaded = false;
}

QString VGiteeImageHosting::giteeImageBedGenerateParam(const QString &p_imagePath, const QString &p_token)
{
    // According to the requirements of GitHub interface, pictures must be in Base64 format.
    // Image to base64.
    QByteArray hexed;
    QFile imgFile(p_imagePath);
    imgFile.open(QIODevice::ReadOnly);
    hexed = imgFile.readAll().toBase64();

    QString imgBase64 = hexed;
    QJsonObject json;
    json.insert("access_token", p_token);
    json.insert("message", QString("updatetest"));
    json.insert("content", imgBase64);

    QJsonDocument document;
    document.setObject(json);
    QByteArray byteArray = document.toJson(QJsonDocument::Compact);
    QString jsonStr(byteArray);
    return jsonStr;
}

VWechatImageHosting::VWechatImageHosting(VFile *p_file, QWidget *p_parent)
    :QObject(p_parent),
     m_file(p_file)
{
    reply = Q_NULLPTR;
    imageUploaded = false;
}

void VWechatImageHosting::handleUploadImageToWechatRequested()
{
    qDebug() << "Start processing image upload request to wechat";
    QString appid = g_config->getWechatAppid();
    QString secret = g_config->getWechatSecret();
    if(appid.isEmpty() || secret.isEmpty())
    {
        qDebug() << "Please configure the Wechat image hosting first!";
        QMessageBox::warning(nullptr,
                             tr("Wechat Image Hosting"),
                             tr("Please configure the Wechat image hosting first!"));
        return;
    }

    authenticateWechatImageHosting(appid, secret);
}

void VWechatImageHosting::authenticateWechatImageHosting(const QString p_appid, const QString p_secret)
{
    qDebug() << "Start certification";
    // Set the mouse to wait
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QNetworkRequest request;
    QString auth_url = "https://api.weixin.qq.com/cgi-bin/token?grant_type=client_credential&appid=" +
                       p_appid.toLocal8Bit() +
                       "&secret=" +
                       p_secret.toLocal8Bit();
    QUrl url = QUrl(auth_url);
    request.setUrl(url);
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }
    reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &VWechatImageHosting::wechatImageBedAuthFinished);
}

void VWechatImageHosting::wechatImageBedAuthFinished()
{
    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();
        QJsonDocument document = QJsonDocument::fromJson(bytes);
        if(!document.isNull())
        {
            if(document.isObject()){
                QJsonObject object = document.object();
                if(object.contains("access_token"))
                {
                    QJsonValue value = object.value("access_token");
                    if(value.isString())
                    {
                        qDebug() << "Authentication successful, get token";
                        // Parsing token.
                        wechatAccessToken = value.toString();

                        qDebug() << "The current article path is: " << m_file->fetchPath();
                        imageBasePath = m_file->fetchBasePath();
                        newFileContent = m_file->getContent();

                        QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,ImageLink::LocalRelativeInternal);
                        QApplication::restoreOverrideCursor();  // Recovery pointer
                        if(images.size() > 0)
                        {
                            proDlg = new QProgressDialog(tr("Uploading images to wechat..."),
                                                         tr("Abort"),
                                                         0,
                                                         images.size(),
                                                         nullptr);
                            proDlg->setWindowModality(Qt::WindowModal);
                            proDlg->setWindowTitle(tr("Uploading Images To wechat"));
                            proDlg->setMinimumDuration(1);
                            uploadImageCount = images.size();
                            uploadImageCountIndex  = uploadImageCount;
                            for(int i=0;i<images.size() ;i++)
                            {
                                if(images[i].m_url.contains(".png") || images[i].m_url.contains(".jpg"))
                                {
                                    imageUrlMap.insert(images[i].m_url, "");
                                }
                                else
                                {
                                    delete proDlg;
                                    imageUrlMap.clear();
                                    qDebug() << "Unsupported type...";
                                    QFileInfo file_info(images[i].m_path.toLocal8Bit());
                                    QString file_suffix = file_info.suffix();
                                    QString info = tr("Unsupported type: ") + file_suffix;
                                    QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
                                    return;
                                }
                            }
                            wechatImageBedUploadManager();
                        }
                        else
                        {
                            qDebug() << m_file->getName() << " No pictures to upload";
                            QString info = m_file->getName() + tr(" No pictures to upload");
                            QMessageBox::information(nullptr, tr("Wechat Image Hosting"), info);
                        }
                    }
                }
                else
                {
                    qDebug() << "Authentication failed";
                    QString string = bytes;
                    qDebug() << string;
                    // You can refine the error here.
                    QApplication::restoreOverrideCursor();
                    if(string.contains("invalid ip"))
                    {
                        QString ip = string.split(" ")[2];
                        QClipboard *board = QApplication::clipboard();
                        board->setText(ip);
                        QMessageBox::warning(nullptr,
                                             tr("Wechat Image Hosting"),
                                             tr("Your ip address was set to the Clipboard!") +
                                             tr("\nPlease add the  IP address: ") +
                                             ip +
                                             tr(" to the wechat ip whitelist!"));
                    }
                    else
                    {
                        QMessageBox::warning(nullptr,
                                             tr("Wechat Image Hosting"),
                                             tr("Please check your Wechat Image Hosting parameters !!\n") +
                                             string);
                    }
                    return;
                }
            }
        }
        else
        {
            delete proDlg;
            imageUrlMap.clear();
            qDebug() << "Resolution failure!";
            qDebug() << "Resolution failure's json: " << bytes;
            QString info = tr("Json decode error, Please contact the developer~");
            QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
        }

        break;
    }
    default:
    {
        QApplication::restoreOverrideCursor();
        qDebug() << "Network error: " << reply->errorString() << " error " << reply->error();
        QString info = tr("Network error: ") + reply->errorString() + tr("\n\nPlease check your network!");
        QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
    }
    }
}

void VWechatImageHosting::wechatImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString image_to_upload = "";
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == "")
        {
            image_to_upload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(image_to_upload));
            break;
        }
    }

    if(image_to_upload == "")
    {
        qDebug() << "All pictures have been uploaded";
        // Copy content to clipboard.
        wechatImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += image_to_upload;
    currentUploadRelativeImagePah = image_to_upload;
    wechatImageBedUploadImage(path, wechatAccessToken);
}

void VWechatImageHosting::wechatImageBedUploadImage(const QString p_imagePath, const QString p_token)
{
    qDebug() << "To deal with: " << p_imagePath;
    QFileInfo fileInfo(p_imagePath.toLocal8Bit());
    if(!fileInfo.exists())
    {
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "The picture does not exist in this path: " << p_imagePath.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + p_imagePath.toLocal8Bit();
        QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
        return;
    }

    QString file_suffix = fileInfo.suffix();  // File extension.
    QString file_name = fileInfo.fileName();  // Filename.
    if(file_suffix != QString::fromLocal8Bit("jpg") && file_suffix != QString::fromLocal8Bit("png"))
    {
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + file_suffix;
        QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
        return;
    }

    qint64 file_size = fileInfo.size();  // Unit is byte.
    qDebug() << "Image size: " << file_size;
    if(file_size > 1024*1024)
    {
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "The size of the picture is more than 1M";
        QString info = tr("The size of the picture is more than 1M! Wechat API does not support!!");
        QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
        return;
    }

    QString upload_img_url = "https://api.weixin.qq.com/cgi-bin/media/uploadimg?access_token=" + p_token;

    QNetworkRequest request;
    request.setUrl(upload_img_url);
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    QString filename = p_imagePath.split(QDir::separator()).last();
    QString contentVariant = QString("form-data; name=\"media\"; filename=\"%1\";").arg(filename);
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(contentVariant));
    QFile *file = new QFile(p_imagePath);
    if(!file->open(QIODevice::ReadOnly))
    {
        qDebug() << "File open failed";
    }
    imagePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(imagePart);

    // Set boundary
    // Because boundary is quoted by QNetworkAccessManager, the wechat api is not recognized...
    QByteArray m_boundary;
    m_boundary.append("multipart/form-data; boundary=");
    m_boundary.append(multiPart->boundary());
    request.setRawHeader(QByteArray("Content-Type"), m_boundary);

    reply = manager.post(request, multiPart);
    multiPart->setParent(reply);

    qDebug() << "Start uploading images: " + p_imagePath + " Waiting for upload to complete";
    uploadImageStatus=true;
    currentUploadImage = p_imagePath;
    connect(reply, &QNetworkReply::finished, this, &VWechatImageHosting::wechatImageBedUploadFinished);
}

void VWechatImageHosting::wechatImageBedUploadFinished()
{
    if(proDlg->wasCanceled())
    {
        qDebug() << "User stops uploading";
        reply->abort();
        // If the upload was successful, don't use it!!!
        imageUrlMap.clear();
        return;
    }

    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();

        QJsonDocument document = QJsonDocument::fromJson(bytes);
        if(!document.isNull())
        {
            if(document.isObject())
            {
                QJsonObject object = document.object();
                if(object.contains("url"))
                {
                    QJsonValue value = object.value("url");
                    if(value.isString())
                    {
                        qDebug() << "Authentication successful, get online link";
                        imageUploaded = true;
                        proDlg->setValue(uploadImageCount);

                        imageUrlMap.insert(currentUploadRelativeImagePah, value.toString());
                        newFileContent.replace(currentUploadRelativeImagePah, value.toString());
                        // Start calling the method.
                        // Whether the value in the map is empty determines whether to stop
                        wechatImageBedUploadManager();
                    }
                }
                else
                {
                    delete proDlg;
                    imageUrlMap.clear();
                    qDebug() << "Upload failure: ";
                    QString error = bytes;
                    qDebug() << bytes;
                    QString info = tr("upload failed! Please contact the developer~");
                    QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
                }
            }
        }
        else
        {
            delete proDlg;
            imageUrlMap.clear();
            qDebug() << "Resolution failure!";
            qDebug() << "Resolution failure's json: " << bytes;
            QString info = tr("Json decode error, Please contact the developer~");
            QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
        }

        break;
    }
    default:
    {
        delete proDlg;
        qDebug()<<"Network error: " << reply->errorString() << " error " << reply->error();

        QString info = tr("Uploading ") +
                       currentUploadImage +
                       tr(" \n\nNetwork error: ") +
                       reply->errorString() +
                       tr("\n\nPlease check the network or image size");
        QMessageBox::warning(nullptr, tr("Wechat Image Hosting"), info);
    }
    }
}

void VWechatImageHosting::wechatImageBedReplaceLink(QString &p_fileContent, const QString &p_filePath)
{
    if(!g_config->getWechatKeepImgScale())
    {
        // delete image scale
        p_fileContent.replace(QRegExp("\\s+=\\d+x"),"");
    }

    if(!g_config->getWechatDoNotReplaceLink())
    {
        // Write content to file.
        QFile file(p_filePath);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        file.write(p_fileContent.toUtf8());
        file.close();
    }

    // Write content to clipboard.
    QClipboard *board = QApplication::clipboard();
    board->setText(p_fileContent);
    QString url = g_config->getMarkdown2WechatToolUrl();
    if(url.isEmpty())
    {
        QMessageBox::warning(nullptr,
                             tr("Wechat Image Hosting"),
                             tr("The article has been copied to the clipboard. Please find a text file and save it!!"));
    }
    else
    {
        QMessageBox::StandardButton result;
        result = QMessageBox::question(nullptr,
                                       tr("Wechat Image Hosting"),
                                       tr("The article has been copied to the clipboard.") +
                                       tr("Do you want to open the tool link of mark down to wechat?"),
                                       QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes);
        if(result == QMessageBox::Yes)
        {
            QDesktopServices::openUrl(QUrl(url));
        }
    }

    // Reset.
    imageUrlMap.clear();
    imageUploaded = false;
}

VTencentImageHosting::VTencentImageHosting(VFile *p_file, QWidget *p_parent)
    :QObject(p_parent),
     m_file(p_file)
{
    reply = Q_NULLPTR;
    imageUploaded = false;
}

void VTencentImageHosting::handleUploadImageToTencentRequested()
{
    qDebug() << "Start processing the image upload request to Tencent";

    if(g_config->getTencentAccessDomainName().isEmpty() ||
       g_config->getTencentSecretId().isEmpty() ||
       g_config->getTencentSecretKey().isEmpty())
    {
        qDebug() << "Please configure the Tencent image hosting first!";
        QMessageBox::warning(nullptr,
                             tr("Tencent Image Hosting"),
                             tr("Please configure the Tencent image hosting first!"));
        return;
    }

    findAndStartUploadImage();
}

void VTencentImageHosting::findAndStartUploadImage()
{
    qDebug() << "The current article path is: " << m_file->fetchPath();
    imageBasePath = m_file->fetchBasePath();
    newFileContent = m_file->getContent();

    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,ImageLink::LocalRelativeInternal);
    if(images.size() > 0)
    {
        proDlg = new QProgressDialog(tr("Uploading images to tencent..."),
                                     tr("Abort"),
                                     0,
                                     images.size(),
                                     nullptr);
        proDlg->setWindowModality(Qt::WindowModal);
        proDlg->setWindowTitle(tr("Uploading Images To Tencent"));
        proDlg->setMinimumDuration(1);

        uploadImageCount = images.size();
        uploadImageCountIndex  = uploadImageCount;
        for(int i=0;i<images.size() ;i++)
        {
            if(images[i].m_url.contains(".png") || images[i].m_url.contains(".jpg"))
            {
                imageUrlMap.insert(images[i].m_url, "");
            }
            else
            {
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Unsupported type...";
                QFileInfo fileInfo(images[i].m_path.toLocal8Bit());
                QString fileSuffix = fileInfo.suffix();
                QString info = tr("Unsupported type: ") + fileSuffix;
                QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), info);
                return;
            }
        }
        tencentImageBedUploadManager();
    }
    else
    {
        qDebug() << m_file->getName() << " No images to upload";
        QString info = m_file->getName() + " No pictures to upload";
        QMessageBox::information(nullptr, tr("Tencent Image Hosting"), info);
    }
}

void VTencentImageHosting::tencentImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString image_to_upload = "";
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == "")
        {
            image_to_upload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(image_to_upload));
            break;
        }
    }

    if(image_to_upload == "")
    {
        qDebug() << "All pictures have been uploaded";
        tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += image_to_upload;
    currentUploadRelativeImagePah = image_to_upload;
    tencentImageBedUploadImage(path,
                               g_config->getTencentAccessDomainName(),
                               g_config->getTencentSecretId(),
                               g_config->getTencentSecretKey());
}

void VTencentImageHosting::tencentImageBedUploadImage(const QString &p_imagePath,
                                                      const QString &p_accessDomainName,
                                                      const QString &p_secretId,
                                                      const QString &p_secretKey)
{
    QFileInfo fileInfo(p_imagePath.toLocal8Bit());
    if(!fileInfo.exists())
    {
        qDebug() << "The picture does not exist in this path: " << p_imagePath.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + p_imagePath.toLocal8Bit();
        QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QString fileSuffix = fileInfo.suffix();  // file extension
    QString fileName = fileInfo.fileName();  // filename
    QString uploadUrl;  // Image upload URL
    new_file_name = QString::number(QDateTime::currentDateTime().toTime_t()) +"_" + fileName;

    if(fileSuffix != QString::fromLocal8Bit("jpg") && fileSuffix != QString::fromLocal8Bit("png"))
    {
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + fileSuffix;
        QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded)
        {
            tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QByteArray postData = getImgContent(p_imagePath);
    QNetworkRequest request;
    uploadUrl = "https://" + p_accessDomainName + "/" + new_file_name;
    request.setRawHeader(QByteArray("Host"), p_accessDomainName.toUtf8());
    if(fileSuffix == QString::fromLocal8Bit("jpg"))
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    }
    else if(fileSuffix == QString::fromLocal8Bit("png"))
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
    }

    request.setHeader(QNetworkRequest::ContentLengthHeader, postData.size());
    QString str = getAuthorizationString(p_secretId, p_secretKey, new_file_name);
    request.setRawHeader(QByteArray("Authorization"), str.toUtf8());
    request.setRawHeader(QByteArray("Connection"), QByteArray("close"));

    request.setUrl(QUrl(uploadUrl));
    if(reply != Q_NULLPTR)
    {
        reply->deleteLater();
    }

    reply = manager.put(request, postData);

    qDebug() << "Start uploading images: " + p_imagePath + " Waiting for upload to complete";
    uploadImageStatus = true;
    currentUploadImage = p_imagePath;
    connect(reply, &QNetworkReply::finished, this, &VTencentImageHosting::tencentImageBedUploadFinished);
}

void VTencentImageHosting::tencentImageBedUploadFinished()
{
    if(proDlg->wasCanceled())
    {
        qDebug() << "User stops uploading";
        reply->abort();        // Stop network request
        imageUrlMap.clear();
        // The ones that have been uploaded successfully before still need to stay
        if(imageUploaded)
        {
            tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    switch (reply->error())
    {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();
        QString replyContent = bytes;
        if(replyContent.size() > 0)
        {
            // If there is a value returned, it means there is a problem.
            qDebug() << "Upload failure";
            qDebug() << replyContent;
            delete proDlg;
            imageUrlMap.clear();
            if(imageUploaded)
            {
                tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
            }
            QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), replyContent);
        }
        else
        {
            qDebug() <<  "Upload success";
            imageUploaded = true;  // On behalf of successfully uploaded images
            proDlg->setValue(uploadImageCount);

            QString downloadUrl = "https://" +
                                  g_config->getTencentAccessDomainName() +
                                  "/" +
                                  new_file_name;

            imageUrlMap.insert(currentUploadRelativeImagePah, downloadUrl);
            newFileContent.replace(currentUploadRelativeImagePah, downloadUrl);
            // Start calling the method.
            // Whether the value in the map is empty determines whether to stop.
            tencentImageBedUploadManager();
        }
        break;
    }
    default:
    {
        delete proDlg;
        imageUrlMap.clear();
        if(imageUploaded)
        {
            tencentImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }

        qDebug()<<"network error: " << reply->error();
        if(reply->error() == 3)  // HostNotFoundError
        {
            QString info = tr(" \n\nNetwork error: HostNotFoundError") +
                           tr("\n\nPlease check your network");
            QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), info);
        }
        else
        {
            QByteArray bytes = reply->readAll();
            QString replyContent = bytes;
            qDebug() << replyContent;

            QString errorCode;
            QString errorMessage;
            QRegExp rc("<Code>([a-zA-Z0-9 ]+)</Code>");
            QRegExp rx("<Message>([a-zA-Z0-9 ]+)</Message>");
            if(rc.indexIn(replyContent) != -1)
            {
                qDebug() << "Error Code: " + rc.cap(1);
                errorCode = rc.cap(1);
            }
            if(rx.indexIn(replyContent) != -1)
            {
                qDebug() << "Error Message: " + rx.cap(1);
                errorMessage = rx.cap(1);
            }

            QString info = tr("Uploading ") +
                           currentUploadImage +
                           "\n\n" +
                           errorCode +
                           "\n\n" +
                           errorMessage;
            QMessageBox::warning(nullptr, tr("Tencent Image Hosting"), info);
        }
    }
    }
}

void VTencentImageHosting::tencentImageBedReplaceLink(QString &p_fileContent, const QString &p_filePath)
{
    if(!g_config->getTencentKeepImgScale())
    {
        // delete image scale
        p_fileContent.replace(QRegExp("\\s+=\\d+x"),"");
    }

    if(!g_config->getTencentDoNotReplaceLink())
    {
        // Write content to file.
        QFile file(p_filePath);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        file.write(p_fileContent.toUtf8());
        file.close();
    }

    // Write content to clipboard.
    QClipboard *board = QApplication::clipboard();
    board->setText(p_fileContent);
    QMessageBox::warning(nullptr,
                         tr("Tencent Image Hosting"),
                         tr("The article has been copied to the clipboard!!"));

    // Reset.
    imageUrlMap.clear();
    imageUploaded = false;
}

QByteArray VTencentImageHosting::hmacSha1(const QByteArray &p_key, const QByteArray &p_baseString)
{
    int blockSize = 64;
    QByteArray key = p_key;
    if (key.length() > blockSize)
    {
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
    }
    QByteArray innerPadding(blockSize, char(0x36));
    QByteArray outerPadding(blockSize, char(0x5c));

    for (int i = 0; i < key.length(); i++)
    {
        innerPadding[i] = innerPadding[i] ^ key.at(i);
        outerPadding[i] = outerPadding[i] ^ key.at(i);
    }
    QByteArray total = outerPadding;
    QByteArray part = innerPadding;
    part.append(p_baseString);

    total.append(QCryptographicHash::hash(part, QCryptographicHash::Sha1));
    QByteArray hashed = QCryptographicHash::hash(total, QCryptographicHash::Sha1);

    return hashed.toHex();
}

QString VTencentImageHosting::getAuthorizationString(const QString &p_secretId,
                                                     const QString &p_secretKey,
                                                     const QString &p_imgName)
{
    unsigned int now=QDateTime::currentDateTime().toTime_t();
    QString keyTime=QString::number(now) + ";" + QString::number(now+3600);
    QString SignKey=hmacSha1(p_secretKey.toUtf8(), keyTime.toUtf8());
    QString HttpString="put\n/" + p_imgName + "\n\n\n";
    QString StringToSign = "sha1\n" +
                            keyTime +
                            "\n" +
                            QCryptographicHash::hash(HttpString.toUtf8(), QCryptographicHash::Sha1).toHex() +
                            "\n";
    QString signature=hmacSha1(SignKey.toUtf8(), StringToSign.toUtf8());
    QString Authorization;
    Authorization = "q-sign-algorithm=sha1";
    Authorization += "&q-ak=";
    Authorization += p_secretId;
    Authorization += "&q-sign-time=";
    Authorization += keyTime;
    Authorization += "&q-key-time=";
    Authorization += keyTime;
    Authorization += "&q-header-list=";
    Authorization += "&q-url-param-list=";
    Authorization += "&q-signature=";
    Authorization += signature;
    return Authorization;
}

QByteArray VTencentImageHosting::getImgContent(const QString &p_imagePath)
{
    QFile file(p_imagePath);
    file.open(QIODevice::ReadOnly);
    QByteArray fdata = file.readAll();
    file.close();
    return fdata;
}
