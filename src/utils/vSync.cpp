#include "vSync.h"
#include <qglobal.h>
#include <qdatetime.h>
#include <qdebug.h>
#include <qstring.h>
#include <qmessagebox.h>
#include <qpushbutton.h>

VSync::VSync(QWidget *parent) : QObject(parent)
{
	_process = new QProcess(this);
	connect(_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadOutput()));
	connect(_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadError()));
	connect(_process, SIGNAL(finished(int)), this, SLOT(onProcessFinish(int)));

	_messageBox = new QMessageBox(parent);
	_messageBox->setModal(true);
	_messageBox->setWindowTitle(tr("Sync"));
	_messageBox->setStandardButtons(QMessageBox::NoButton);
	_messageButton = new QPushButton(_messageBox);
	_messageButton->setText(tr("Sure"));
	connect(_messageButton, &QPushButton::clicked, this, &VSync::onMessageButtonClick);
}

VSync::~VSync()
{
	_process->close();
}

void VSync::status()
{
	this->_type = SyncType::Status;
	this->start(getSyncHead("status"));
}

void VSync::add()
{
	this->_type = SyncType::Add;
	this->start(getSyncHead("add -A"));
}

void VSync::commit()
{
	this->_type = SyncType::Commit;
	QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd-hh:mm:ss");
	this->start(getSyncHead(QString("commit -m %1").arg(time)));
}

void VSync::push()
{
	this->_type = SyncType::Push;
	this->start(getSyncHead("push"));
}

void VSync::pull()
{
	this->_type = SyncType::Pull;
	this->start(getSyncHead("pull"));
}

void VSync::authentication()
{
	this->_type = SyncType::Authentication;
	this->start("git config --global credential.helper store");
}

void VSync::download()
{
	showMessageBox(tr("Downloading"), false);
	this->_target = SyncTarget::Download;
	this->status();
}

void VSync::upload()
{
	showMessageBox(tr("Uploading"), false);
	this->_target = SyncTarget::Upload;
	this->status();
}

void VSync::onReadOutput()
{
	QString output = _process->readAllStandardOutput();
	qDebug() << "VSync.onReadOutput: " << output;
	_output.append(output);
}

void VSync::onReadError()
{
	QString error = _process->readAllStandardError();
	qDebug() << "VSync.onReadError: " << error;
	_error.append(error);
}

void VSync::onProcessFinish(int exitCode)
{
	qInfo() << "VSync.onProcessFinish: " << exitCode;
	if (exitCode == 0)
	{
		switch (this->_target)
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
		qCritical() << "sync failed, error: " << _error << ", info: " << _output;
		QString message = QString("sync failed, exitCode: %1, error: %2, info: %3").arg(exitCode).arg(_error).arg(_output);
		showMessageBox(message, true);
	}

	_error.clear();
	_output.clear();
}

void VSync::start(const QString &cmd)
{
	_process->start("cmd", QStringList() << "/c" << cmd);
	_process->waitForStarted();
}

void VSync::showMessageBox(const QString &message, bool showButton)
{
	_messageBox->setText(message);
	if (showButton)
	{
		_messageBox->addButton(_messageButton, QMessageBox::ButtonRole::YesRole);
	}
	else
	{
		_messageBox->removeButton(_messageButton);
	}

	if (!_messageBox->isVisible())
	{
		_messageBox->setVisible(true);
	}
}

void VSync::hideMessageBox()
{
	_messageBox->removeButton(_messageButton);
	_messageBox->setVisible(false);
}

void VSync::onMessageButtonClick()
{
	_messageBox->hide();
}

void VSync::processDownload()
{
	switch (this->_type)
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
	switch (this->_type)
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
	_type = VSync::SyncType::None;
	_target = VSync::SyncTarget::None;
	emit this->downloadSuccess();
}

void VSync::uploadFinish()
{
	qInfo() << "upload finish";
	showMessageBox(tr("Upload Success"), true);
	_type = VSync::SyncType::None;
	_target = VSync::SyncTarget::None;
	emit this->uploadSuccess();
}