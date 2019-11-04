#include "vimageHosting.h"
#include "utils/vutils.h"
#include "vedittab.h"

extern VConfigManager *g_config;

VImageHosting::VImageHosting(VFile *p_file, QWidget *p_parent)
    : m_file(p_file),
      parent(p_parent)
{
    reply = Q_NULLPTR;
    imageUploaded = false;
}

void VImageHosting::handleUploadImageToGithubRequested()
{
    qDebug() << "Start processing the image upload request to GitHub";

    if(g_config->getpersonalAccessToken().isEmpty() || g_config->getReposName().isEmpty() || g_config->getUserName().isEmpty())
    {
        qDebug() << "Please configure the GitHub image hosting first!";
        QMessageBox::warning(parent, tr("Github Image Hosting"), tr("Please configure the GitHub image hosting first!"));
        return;
    }

    authenticateGithubImageHosting(g_config->getpersonalAccessToken());
}


void VImageHosting::authenticateGithubImageHosting(QString p_token)
{
    qDebug() << "start the authentication process ";
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QNetworkRequest request;
    QUrl url = QUrl("https://api.github.com");
    QString ptoken = "token " + p_token;
    request.setRawHeader("Authorization", ptoken.toLocal8Bit());
    request.setUrl(url);
    if(reply != Q_NULLPTR) {
        reply->deleteLater();
    }
    reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &VImageHosting::githubImageBedAuthFinished);
}

void VImageHosting::githubImageBedAuthFinished()
{
    switch (reply->error()) {
    case QNetworkReply::NoError:
    {
        QByteArray bytes = reply->readAll();

        if(bytes.contains("Bad credentials")){
            qDebug() << "Authentication failed";
            QApplication::restoreOverrideCursor();  // Recovery pointer
            QMessageBox::warning(parent, tr("Github Image Hosting"), tr("Bad credentials!! Please check your Github Image Hosting parameters !!"));
            return;
        }else{
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
                                       parent);
                proDlg->setWindowModality(Qt::WindowModal);
                proDlg->setWindowTitle(tr("Uploading Images To Github"));
                proDlg->setMinimumDuration(1);

                uploadImageCount = images.size();
                uploadImageCountIndex  = uploadImageCount;
                for(int i=0;i<images.size() ;i++)
                {
                    if(images[i].m_url.contains(".png") || images[i].m_url.contains(".jpg")|| images[i].m_url.contains(".gif")){
                        imageUrlMap.insert(images[i].m_url,"");
                    }else{
                        delete proDlg;
                        imageUrlMap.clear();
                        qDebug() << "Unsupported type...";
                        QFileInfo fileInfo(images[i].m_path.toLocal8Bit());
                        QString fileSuffix = fileInfo.suffix();
                        QString info = tr("Unsupported type: ") + fileSuffix;
                        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
                        return;
                    }
                }
                githubImageBedUploadManager();
            }
            else
            {
                qDebug() << m_file->getName() << " No images to upload";
                QString info = m_file->getName() + " No pictures to upload";
                QMessageBox::information(NULL, tr("Github Image Hosting"), info);
            }
        }
        break;
    }
    default:
    {
        QApplication::restoreOverrideCursor();  // Recovery pointer
        qDebug() << "Network error: " << reply->errorString() << " error " << reply->error();
        QString info = tr("Network error: ") + reply->errorString();
        QMessageBox::warning(parent, tr("Github Image Hosting"), info);
    }
    }
}

void VImageHosting::githubImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString imageToUpload = "";
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == ""){
            imageToUpload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(imageToUpload));
            break;
        }
    }

    if(imageToUpload == ""){
        qDebug() << "All images have been uploaded";
        githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    if(g_config->getpersonalAccessToken().isEmpty() || g_config->getReposName().isEmpty() || g_config->getUserName().isEmpty())
    {
        qDebug() << "Please configure the GitHub image hosting first!";
        QMessageBox::warning(parent, tr("Github Image Hosting"), tr("Please configure the GitHub image hosting first!"));
        imageUrlMap.clear();
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += imageToUpload;
    githubImageBedUploadImage(g_config->getUserName(), g_config->getReposName(), path, g_config->getpersonalAccessToken());
}

void VImageHosting::githubImageBedUploadImage(QString username, QString repository, QString imagePath, QString token)
{
    QFileInfo fileInfo(imagePath.toLocal8Bit());
    if(!fileInfo.exists()){
        qDebug() << "The picture does not exist in this path: " << imagePath.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + imagePath.toLocal8Bit();
        QMessageBox::warning(parent, tr("Github Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded){
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QString fileSuffix = fileInfo.suffix();  // file extension
    QString fileName = fileInfo.fileName();  // filename
    QString uploadUrl;  // Image upload URL
    uploadUrl = "https://api.github.com/repos/" + username + "/" + repository + "/contents/"  +  QString::number(QDateTime::currentDateTime().toTime_t()) +"_" + fileName;
    if(fileSuffix != QString::fromLocal8Bit("jpg") && fileSuffix != QString::fromLocal8Bit("png") && fileSuffix != QString::fromLocal8Bit("gif")){
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + fileSuffix;
        QMessageBox::warning(parent, tr("Github Image Hosting"), info);
        imageUrlMap.clear();
        if(imageUploaded){
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    QNetworkRequest request;
    QUrl url = QUrl(uploadUrl);
    QString ptoken = "token " + token;
    request.setRawHeader("Authorization", ptoken.toLocal8Bit());
    request.setUrl(url);
    if(reply != Q_NULLPTR) {
        reply->deleteLater();
    }

    QString param = githubImageBedGenerateParam(imagePath);
    QByteArray postData;
    postData.append(param);
    reply = manager.put(request, postData);
    qDebug() << "Start uploading images: " + imagePath + " Waiting for upload to complete";
    uploadImageStatus = true;
    currentUploadImage = imagePath;
    connect(reply, &QNetworkReply::finished, this, &VImageHosting::githubImageBedUploadFinished);
}

void VImageHosting::githubImageBedUploadFinished()
{
    if (proDlg->wasCanceled()) {
        qDebug() << "User stops uploading";
        reply->abort();        // Stop network request
        imageUrlMap.clear();
        // The ones that have been uploaded successfully before still need to stay
        if(imageUploaded){
            githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
        }
        return;
    }

    switch (reply->error()) {
        case QNetworkReply::NoError:
        {
            QByteArray bytes = reply->readAll();
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(httpStatus == 201){
                qDebug() <<  "Upload success";

                QString downloadUrl;
                QString imageName;
                QJsonDocument doucment = QJsonDocument::fromJson(bytes);
                if (!doucment.isNull() )
                {
                    if (doucment.isObject()) {
                        QJsonObject object = doucment.object();
                        if (object.contains("content")) {
                            QJsonValue value = object.value("content");
                            if (value.isObject()) {
                                QJsonObject obj = value.toObject();
                                if (obj.contains("download_url")) {
                                    QJsonValue value = obj.value("download_url");
                                    if (value.isString()) {
                                        downloadUrl = value.toString();
                                        qDebug() << "json decode: download_url : " << downloadUrl;
                                        imageUploaded = true;  // On behalf of successfully uploaded images
                                        proDlg->setValue(uploadImageCount);
                                    }
                                }
                                if(obj.contains("name")){
                                    QJsonValue value = obj.value("name");
                                    if(value.isString()){
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
                                // Start calling the method. Whether the value in the map is empty determines whether to stop
                                githubImageBedUploadManager();
                            }
                        }
                    }
                }
                else{
                    delete proDlg;
                    imageUrlMap.clear();
                    qDebug() << "Resolution failure!";
                    qDebug() << "Resolution failure's json: " << bytes;
                    if(imageUploaded){
                        githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
                    }
                    QString info = tr("Json decode error, Please contact the developer~");
                    QMessageBox::warning(parent, tr("Github Image Hosting"), info);
                }


            }else{
                // If status is not 201, it means there is a problem
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Upload failure";
                if(imageUploaded){
                    githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
                }
                QString info = tr("github status code != 201, Please contact the developer~");
                QMessageBox::warning(parent, tr("Github Image Hosting"), info);
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

            if(imageUploaded){
                githubImageBedReplaceLink(newFileContent, m_file->fetchPath());
            }
            QString info = tr("Uploading ") + currentUploadImage + tr(" \n\nNetwork error: ") + reply->errorString() + tr("\n\nPlease check the network or image size");
            QMessageBox::warning(parent, tr("Github Image Hosting"), info);
        }
    }
}

void VImageHosting::githubImageBedReplaceLink(QString fileContent, QString filePath)
{
    // This function must be executed when the upload is completed or fails in the middle
    // Write content to file
    QFile file(filePath);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(fileContent.toUtf8());
    file.close();
    // Reset
    imageUrlMap.clear();
    imageUploaded = false;
}

QString VImageHosting::githubImageBedGenerateParam(QString imagePath){
    // According to the requirements of GitHub interface, pictures must be in Base64 format
    // img to base64
    QByteArray hexed;
    QFile imgFile(imagePath);
    imgFile.open(QIODevice::ReadOnly);
    hexed = imgFile.readAll().toBase64();

    QString imgBase64 = hexed;  // Base64 encoding of images
    QJsonObject json;
    json.insert("message", QString("updatetest"));
    json.insert("content", imgBase64);

    QJsonDocument document;
    document.setObject(json);
    QByteArray byteArray = document.toJson(QJsonDocument::Compact);
    QString jsonStr(byteArray);
    return jsonStr;
}

void VImageHosting::handleUploadImageToWechatRequested()
{
    qDebug() << "Start processing image upload request to wechat";
    QString appid = g_config->getAppid();
    QString secret = g_config->getSecret();
    if(appid.isEmpty() || secret.isEmpty())
    {
        qDebug() << "Please configure the Wechat image hosting first!";
        QMessageBox::warning(parent, tr("Wechat Image Hosting"), tr("Please configure the Wechat image hosting first!"));
        return;
    }

    authenticateWechatImageHosting(appid, secret);
}

void VImageHosting::authenticateWechatImageHosting(QString appid, QString secret)
{
    qDebug() << "Start certification";
    QApplication::setOverrideCursor(Qt::WaitCursor); // Set the mouse to wait
    QNetworkRequest request;
    QString auth_url = "https://api.weixin.qq.com/cgi-bin/token?grant_type=client_credential&appid="+ appid.toLocal8Bit() + "&secret=" + secret.toLocal8Bit();
    QUrl url = QUrl(auth_url);
//    request.setRawHeader("grant_type", "client_credential");
//    request.setRawHeader("appid", appid.toLocal8Bit());
//    request.setRawHeader("secret", secret.toLocal8Bit());
    request.setUrl(url);
    if(reply != Q_NULLPTR) {
        reply->deleteLater();
    }
    reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, &VImageHosting::wechatImageBedAuthFinished);
}

void VImageHosting::wechatImageBedAuthFinished()
{
    switch (reply->error()) {
        case QNetworkReply::NoError:
        {
            QByteArray bytes = reply->readAll();
            QJsonDocument document = QJsonDocument::fromJson(bytes);
            if(!document.isNull()){
                if(document.isObject()){
                    QJsonObject object = document.object();
                    if(object.contains("access_token")){
                        QJsonValue value = object.value("access_token");
                        if(value.isString()){
                            qDebug() << "Authentication successful, get token";
                            // Parsing token
                            wechatAccessToken = value.toString();

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
                                                       parent);
                                proDlg->setWindowModality(Qt::WindowModal);
                                proDlg->setWindowTitle(tr("Uploading Images To Github"));
                                proDlg->setMinimumDuration(1);
                                uploadImageCount = images.size();
                                uploadImageCountIndex  = uploadImageCount;
                                for(int i=0;i<images.size() ;i++)
                                {
                                    if(images[i].m_url.contains(".png") || images[i].m_url.contains(".jpg")){
                                        imageUrlMap.insert(images[i].m_url,"");
                                    }else{
                                        delete proDlg;
                                        imageUrlMap.clear();
                                        qDebug() << "Unsupported type...";
                                        QFileInfo file_info(images[i].m_path.toLocal8Bit());
                                        QString file_suffix = file_info.suffix();
                                        QString info = tr("Unsupported type: ") + file_suffix;
                                        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
                                        return;
                                    }
                                }
                                wechatImageBedUploadManager();
                            }
                            else
                            {
                                qDebug() << m_file->getName() << " No pictures to upload";
                                QString info = m_file->getName() + tr(" No pictures to upload");
                                QMessageBox::information(NULL, tr("Wechat Image Hosting"), info);
                            }
                        }
                    }else{
                        qDebug() << "Authentication failed";
                        QString string = bytes;
                        qDebug() << string;
                        // You can refine the error here
                        QApplication::restoreOverrideCursor();
                        if(string.contains("invalid ip")){
                            QString ip = string.split(" ")[2];
                            QClipboard *board = QApplication::clipboard();
                            board->setText(ip);
                            QMessageBox::warning(parent, tr("Wechat Image Hosting"), tr("Your ip address was set to the Clipboard! \nPlease add the  IP address: ") + ip + tr(" to the wechat ip whitelist!"));
                        }else{
                            QMessageBox::warning(parent, tr("Wechat Image Hosting"), tr("Please check your Wechat Image Hosting parameters !!\n") + string);
                        }
                        return;
                    }
                }
            }else{
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Resolution failure!";
                qDebug() << "Resolution failure's json: " << bytes;
                QString info = tr("Json decode error, Please contact the developer~");
                QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
            }


            break;
        }
        default:
        {
            QApplication::restoreOverrideCursor();
            qDebug() << "Network error: " << reply->errorString() << " error " << reply->error();
            QString info = tr("Network error: ") + reply->errorString();
            QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
        }
    }
}

void VImageHosting::wechatImageBedUploadManager()
{
    uploadImageCountIndex--;

    QString image_to_upload = "";
    QMapIterator<QString, QString> it(imageUrlMap);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == ""){
            image_to_upload = it.key();
            proDlg->setValue(uploadImageCount - 1 - uploadImageCountIndex);
            proDlg->setLabelText(tr("Uploaading image: %1").arg(image_to_upload));
            break;
        }

    }

    if(image_to_upload == ""){
        qDebug() << "All pictures have been uploaded";
        // Copy content to clipboard
        wechatImageBedReplaceLink(newFileContent, m_file->fetchPath());
        return;
    }

    QString path = imageBasePath + QDir::separator();
    path += image_to_upload;
    currentUploadRelativeImagePah = image_to_upload;
    wechatImageBedUploadImage(path, wechatAccessToken);
}

void VImageHosting::wechatImageBedUploadImage(QString image_path, QString token)
{
    qDebug() << "To deal with: " << image_path;
    QFileInfo fileInfo(image_path.toLocal8Bit());
    if(!fileInfo.exists()){
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "The picture does not exist in this path: " << image_path.toLocal8Bit();
        QString info = tr("The picture does not exist in this path: ") + image_path.toLocal8Bit();
        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
        return;
    }

    QString file_suffix = fileInfo.suffix();  // File extension
    QString file_name = fileInfo.fileName();  // filename
    if(file_suffix != QString::fromLocal8Bit("jpg") && file_suffix != QString::fromLocal8Bit("png")){
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "Unsupported type...";
        QString info = tr("Unsupported type: ") + file_suffix;
        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
        return;
    }

    qint64 file_size = fileInfo.size();  // Unit is byte
    qDebug() << "Image size: " << file_size;
    if(file_size > 1024*1024){
        delete proDlg;
        imageUrlMap.clear();
        qDebug() << "The size of the picture is more than 1M";
        QString info = tr("The size of the picture is more than 1M! Wechat API does not support!!");
        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
        return;
    }

    QString upload_img_url = "https://api.weixin.qq.com/cgi-bin/media/uploadimg?access_token=" + token;

    QNetworkRequest request;
    request.setUrl(upload_img_url);
    if(reply != Q_NULLPTR){
        reply->deleteLater();
    }

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    QString filename = image_path.split(QDir::separator()).last();
    QString contentVariant = QString("form-data; name=\"media\"; filename=\"%1\";").arg(filename);
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(contentVariant));
    QFile *file = new QFile(image_path);
    if(!file->open(QIODevice::ReadOnly)){
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


    qDebug() << "Start uploading images: " + image_path + " Waiting for upload to complete";
    uploadImageStatus=true;
    currentUploadImage = image_path;
    connect(reply, &QNetworkReply::finished, this, &VImageHosting::wechatImageBedUploadFinished);

}

void VImageHosting::wechatImageBedUploadFinished()
{
    if(proDlg->wasCanceled()){
        qDebug() << "User stops uploading";
        reply->abort();
        // If the upload was successful, don't use it!!!
        imageUrlMap.clear();
        return;
    }

    switch (reply->error()) {
        case QNetworkReply::NoError:
        {
            QByteArray bytes = reply->readAll();

            //qDebug() << "The returned contents are as follows: ";
            //QString a = bytes;
            //qDebug() << qPrintable(a);

            QJsonDocument document = QJsonDocument::fromJson(bytes);
            if(!document.isNull()){
                if(document.isObject()){
                    QJsonObject object = document.object();
                    if(object.contains("url")){
                        QJsonValue value = object.value("url");
                        if(value.isString()){
                            qDebug() << "Authentication successful, get online link";
                            imageUploaded = true;
                            proDlg->setValue(uploadImageCount);

                            imageUrlMap.insert(currentUploadRelativeImagePah, value.toString());
                            newFileContent.replace(currentUploadRelativeImagePah, value.toString());
                            // Start calling the method. Whether the value in the map is empty determines whether to stop
                            wechatImageBedUploadManager();
                        }
                    }else{
                        delete proDlg;
                        imageUrlMap.clear();
                        qDebug() << "Upload failure: ";
                        QString error = bytes;
                        qDebug() << bytes;
                        QString info = tr("upload failed! Please contact the developer~");
                        QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
                    }
                }
            }else{
                delete proDlg;
                imageUrlMap.clear();
                qDebug() << "Resolution failure!";
                qDebug() << "Resolution failure's json: " << bytes;
                QString info = tr("Json decode error, Please contact the developer~");
                QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);
            }

            break;
        }
        default:
        {
            delete proDlg;
            qDebug()<<"Network error: " << reply->errorString() << " error " << reply->error();

            QString info = tr("Uploading ") + currentUploadImage + tr(" \n\nNetwork error: ") + reply->errorString() + tr("\n\nPlease check the network or image size");
            QMessageBox::warning(parent, tr("Wechat Image Hosting"), info);

        }

    }
}
void VImageHosting::wechatImageBedReplaceLink(QString file_content, QString file_path)
{
    // Write content to clipboard
    QClipboard *board = QApplication::clipboard();
    board->setText(file_content);
    QString url = g_config->getMarkdown2WechatToolUrl();
    if(url.isEmpty()){
        QMessageBox::warning(parent, tr("Wechat Image Hosting"), tr("The article has been copied to the clipboard. Please find a text file and save it!!"));
    }else{
        QMessageBox::StandardButton result;
        result = QMessageBox::question(parent, tr("Wechat Image Hosting"), tr("The article has been copied to the clipboard. Do you want to open the tool link of mark down to wechat?"), QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes);
        if(result == QMessageBox::Yes){
            QDesktopServices::openUrl(QUrl(url));
        }
    }
    imageUrlMap.clear();
    imageUploaded = false;  // reset
}
