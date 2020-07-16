#include "vgit.h"
#include "vgit.h"
#include <qglobal.h>
#include <qdatetime.h>
#include <qdebug.h>
#include <qstring.h>
#include <qmessagebox.h>
#include <qpushbutton.h>

VGit::VGit(QWidget *parent) : QObject(parent)
{
	_process = new QProcess(this);
	connect(_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadOutput()));
	connect(_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadError()));
	//connect(_process, SIGNAL(errorOccurred()), this, SLOT(onReadError()));
	connect(_process, SIGNAL(finished(int)), this, SLOT(onProcessFinish(int)));

	_messageBox = new QMessageBox(parent);
	_messageBox->setModal(true);
	_messageBox->setWindowTitle(tr("git operation"));
	_messageBox->resize(QSize(600,300));
	_messageBox->setStandardButtons(QMessageBox::NoButton);
	_messageButton = new QPushButton(_messageBox);
	_messageButton->setText(tr("sure"));
	connect(_messageButton, &QPushButton::clicked, this, &VGit::onMessageButtonClick);
}

VGit::~VGit()
{
	_process->close();
}

void VGit::status()
{
	this->_type = GitType::Status;
	this->showMessageBox(tr("git status"), false);
	this->start(getGitHead("status"));
}

void VGit::add()
{
	this->_type = GitType::Add;
	showMessageBox(tr("git stash"), false);
	this->start(getGitHead("add -A"));
}

void VGit::commit()
{
	this->_type = GitType::Commit;
	showMessageBox(tr("git coommit"), false);
	QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd-hh:mm:ss");
	this->start(getGitHead(QString("commit -m %1").arg(time)));
}

void VGit::push()
{
	this->_type = GitType::Push;
	showMessageBox(tr("git push"), false);
	this->start(getGitHead("push"));
}

void VGit::pull()
{
	this->_type = GitType::Pull;
	showMessageBox(tr("git pull"), false);
	this->start(getGitHead("pull"));
}

void VGit::authentication()
{
	this->_type = GitType::Authentication;
	showMessageBox(tr("git authentication"), false);
	this->start("git config --global credential.helper store");
}

void VGit::download()
{
	this->_target = GitTarget::Download;
	this->status();
}

void VGit::upload()
{
	this->_target = GitTarget::Upload;
	this->status();
}

void VGit::onReadOutput()
{
	qDebug() << "VGit.onReadOutput: " << _process->readAllStandardOutput();
	_output.append(_process->readAllStandardOutput());
}

void VGit::onReadError()
{
	qDebug() << "VGit.onReadError: " << _process->readAllStandardError();
	_error.append(_process->readAllStandardError());
}

void VGit::onProcessFinish(int exitCode)
{
	qDebug() << "VGit.onProcessFinish: " << exitCode;
	if (exitCode == 0)
	{
		switch (this->_target)
		{
		case GitTarget::Download:
			this->processDownload();
			break;
		case GitTarget::Upload:
			this->processUpload();
			break;
		default:
			break;
		}
	}
	else
	{
		/* code */
		QString message = QString::asprintf("git failed，exitCode: %d, reason:\n%s", exitCode, _error);
		showMessageBox(message, true);
	}
}

void VGit::start(const QString &cmd)
{
	_process->start("cmd", QStringList() << "/c" << cmd);
	_process->waitForStarted();
}

void VGit::showMessageBox(const QString &message, bool showButton)
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

void VGit::hideMessageBox()
{
	_messageBox->removeButton(_messageButton);
	_messageBox->setVisible(false);
}

void VGit::onMessageButtonClick()
{
	_messageBox->hide();
}

void VGit::clear()
{
	_error.clear();
	_output.clear();
	_type = VGit::GitType::None;
}

void VGit::processDownload()
{
	switch (this->_type)
	{
	case GitType::Status:
		this->authentication();
		break;
	case GitType::Authentication:
		this->pull();
		break;
	case GitType::Pull:
		qDebug() << "download finish";	
		showMessageBox(tr("Download Success"), true);
		break;
	default:
		break;
	}
}

void VGit::processUpload()
{
	switch (this->_type)
	{
	case GitType::Status:
		this->add();
		break;
	case GitType::Add:
		this->commit();
		break;
	case GitType::Commit:
		this->authentication();
		break;
	case GitType::Authentication:
		this->push();
		break;
	case GitType::Push:
		qDebug() << "upload finish";		
		showMessageBox(tr("Upload Success"), true);
		break;
	default:
		break;
	}
}