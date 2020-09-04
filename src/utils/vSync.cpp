#include "vSync.h"
#include <qglobal.h>
#include <qdatetime.h>
#include <qdebug.h>
#include <qstring.h>
#include <qmessagebox.h>
#include <qpushbutton.h>

VSync::VSync(QWidget *parent) : QObject(parent)
{
    m_process = new QProcess(this);
    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadOutput()));
    connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadError()));
    connect(m_process, SIGNAL(finished(int)), this, SLOT(onProcessFinish(int)));

    m_messageBox = new QMessageBox(parent);
    m_messageBox->setModal(true);
    m_messageBox->setWindowTitle(tr("Sync"));
    m_messageBox->setStandardButtons(QMessageBox::NoButton);
    m_messageButton = new QPushButton(m_messageBox);
    m_messageButton->setText(tr("Sure"));
    connect(m_messageButton, &QPushButton::clicked, this, &VSync::onMessageButtonClick);
}

VSync::~VSync()
{
    m_process->close();
}

void VSync::status()
{
    this->m_type = SyncType::Status;
    this->start(getSyncHead("status"));
}

void VSync::add()
{
    this->m_type = SyncType::Add;
    this->start(getSyncHead("add -A"));
}

void VSync::commit()
{
    this->m_type = SyncType::Commit;
    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd-hh:mm:ss");
    this->start(getSyncHead(QString("commit -m %1").arg(time)));
}

void VSync::push()
{
    this->m_type = SyncType::Push;
    this->start(getSyncHead("push"));
}

void VSync::pull()
{
    this->m_type = SyncType::Pull;
    this->start(getSyncHead("pull"));
}

void VSync::authentication()
{
    this->m_type = SyncType::Authentication;
    this->start("git config --global credential.helper store");
}

void VSync::download()
{
    showMessageBox(tr("Downloading"), false);
    this->m_target = SyncTarget::Download;
    this->status();
}

void VSync::upload()
{
    showMessageBox(tr("Uploading"), false);
    this->m_target = SyncTarget::Upload;
    this->status();
}

void VSync::onReadOutput()
{
    QString output = m_process->readAllStandardOutput();
    qDebug() << "VSync.onReadOutput: " << output;
    m_output.append(output);
}

void VSync::onReadError()
{
    QString error = m_process->readAllStandardError();
    qDebug() << "VSync.onReadError: " << error;
    m_error.append(error);
}

void VSync::onProcessFinish(int exitCode)
{
    qInfo() << "VSync.onProcessFinish: " << exitCode;
    if (exitCode == 0)
    {
        switch (this->m_target)
        {
        case SyncTarget::Download:
            this->processDownload();
            break;
        case SyncTarget::Upload:
            this->processUpload();
            break;
        default:
            break;
        }
    }
    else
    {
        /* code */
        qCritical() << "sync failed, error: " << m_error << ", info: " << m_output;
        QString message = QString("sync failed, exitCode: %1, error: %2, info: %3").arg(exitCode).arg(m_error).arg(m_output);
        showMessageBox(message, true);
    }

    m_error.clear();
    m_output.clear();
}

void VSync::start(const QString &cmd)
{
#if defined(Q_OS_WIN)
    m_process->start("cmd", QStringList() << "/c" << cmd);
#else
    m_process->start("/bin/sh", QStringList() << "-c" << cmd);
#endif
    m_process->waitForStarted();
}

void VSync::showMessageBox(const QString &message, bool showButton)
{
    m_messageBox->setText(message);
    if (showButton)
    {
        m_messageBox->addButton(m_messageButton, QMessageBox::ButtonRole::YesRole);
    }
    else
    {
        m_messageBox->removeButton(m_messageButton);
    }

    if (!m_messageBox->isVisible())
    {
        m_messageBox->setVisible(true);
    }
}

void VSync::hideMessageBox()
{
    m_messageBox->removeButton(m_messageButton);
    m_messageBox->setVisible(false);
}

void VSync::onMessageButtonClick()
{
    m_messageBox->hide();
}

void VSync::processDownload()
{
    switch (this->m_type)
    {
    case SyncType::Status:
        this->authentication();
        break;
    case SyncType::Authentication:
        this->pull();
        break;
    case SyncType::Pull:
        this->downloadFinish();
        break;
    default:
        break;
    }
}

void VSync::processUpload()
{
    switch (this->m_type)
    {
    case SyncType::Status:
        this->add();
        break;
    case SyncType::Add:
        this->commit();
        break;
    case SyncType::Commit:
        this->authentication();
        break;
    case SyncType::Authentication:
        this->push();
        break;
    case SyncType::Push:
        this->uploadFinish();
        break;
    default:
        break;
    }
}

void VSync::downloadFinish()
{
    qInfo() << "download finish";
    showMessageBox(tr("Download Success"), true);
    m_type = VSync::SyncType::None;
    m_target = VSync::SyncTarget::None;
    emit this->downloadSuccess();
}

void VSync::uploadFinish()
{
    qInfo() << "upload finish";
    showMessageBox(tr("Upload Success"), true);
    m_type = VSync::SyncType::None;
    m_target = VSync::SyncTarget::None;
    emit this->uploadSuccess();
}
