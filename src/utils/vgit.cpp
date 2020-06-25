#include "vgit.h"
#include "vgit.h"
#include <qprocess.h>
#include <qglobal.h>
#include <qdatetime.h>
#include <QDebug>
#include <qstring.h>

VGit* VGit::create(const QString& gitDir, QString& output, QString& error)
{
	Q_ASSERT(gitDir != NULL);

	VGit::execute(VGit::getGitString(gitDir, VGit::GitType::Status), output, error);
	if (!error.isEmpty()) {
		return NULL;
	}

	VGit* git = new VGit(gitDir);

	return git;
}

void VGit::execute(const QString& cmd, QString& output, QString& error)
{
	Q_ASSERT(cmd != NULL);

	qDebug() << "VGit.execute: " << cmd;
	QProcess p(0);
	p.start("cmd", QStringList() << "/c" << cmd);
	p.waitForStarted();
	p.waitForFinished();

	error.clear();
	error.append(QString::fromLocal8Bit(p.readAllStandardError()));

	output.clear();
	output.append(QString::fromLocal8Bit(p.readAllStandardOutput()));
}

QString VGit::getGitString(const QString& gitDir, GitType type, const QString& arg)
{
	QString option;
	switch (type)
	{
	case VGit::GitType::Status:
		option = "status";
		break;
	case VGit::GitType::Add:
		option = "add -A";
		break;
	case VGit::GitType::Commit:
		option = QString("commit -m %1").arg(arg);
		break;
	case VGit::GitType::Push:
		option = "push";
		break;
	case VGit::GitType::Pull:
		option = "pull";
		break;
	default:
		Q_ASSERT(false);
		break;
	}

	qDebug() << "VGit.getGitString: option=" << option;
	return QString("git -C %1 %2").arg(gitDir).arg(option);
}

const QString& VGit::getStandardOutput() const
{
	return this->_output;
}

const QString& VGit::getStandardError() const
{
	return this->_error;
}

bool VGit::isSuccess() const
{
	return this->_error.isEmpty();
}

void VGit::Upload()
{
	//add
	VGit::execute(
		VGit::getGitString(this->_gitDir, VGit::GitType::Add),
		this->_output,
		this->_error
	);
	if (!isSuccess())
	{
		return;
	}

	//commit
	VGit::execute(
		VGit::getGitString(this->_gitDir, VGit::GitType::Commit, QDateTime::currentDateTime().toString("yyyy-MM-dd-hh:mm:ss")),
		this->_output,
		this->_error
	);
	if (!isSuccess())
	{
		return;
	}

	VGit::execute(
		"git config --global credential.helper store",
		this->_output,
		this->_error
	);
	if (!isSuccess())
	{
		return;
	}

	//push
	VGit::execute(
		VGit::getGitString(this->_gitDir, VGit::GitType::Push),
		this->_output,
		this->_error
	);
}

void VGit::Download()
{
	//pull
	VGit::execute(
		VGit::getGitString(this->_gitDir, VGit::GitType::Pull),
		this->_output,
		this->_error
	);
}

// VGit::VGit(const QString& gitDir) : _gitDir(gitDir)
// {
// 	_success = true;
// 	_process = new QProcess(this);
// 	connect(_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadOutput()));
// 	connect(_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadError()));
// }

// VGit::~VGit()
// {
// 	_process->close();
// 	delete _process;
// }

// void VGit::status()
// {
// 	this->_currentType = GitType::Status;
// 	clear();

// 	_process->start("cmd", QStringList() << "/c" << getGitHead("status"))
// 	_process->waitForStarted();
// 	_process->waitForFinished();
// }

// void VGit::add()
// {
// 	this->_currentType = GitType::Add;
// 	clear();

// 	_process->start("cmd", QStringList() << "/c" << getGitHead("add -A"))
// 	_process->waitForStarted();
// 	_process->waitForFinished();
// }

// void VGit::commit()
// {
// 	clear();
// }

// void VGit::push()
// {
// 	clear();
// }

// void VGit::pull()
// {
// 	clear();
// }

// void VGit::onReadOutput()
// {
// }

// void VGit::onReadError()
// {
// 	_success = false;
// }

// void VGit::clear()
// {
// 	_error.clear();
// 	_output.clear();
// 	_success = true;
// 	_currentType = VGit::GitType::None;
// }

// inline QString VGit::getGitHead(const QString& args)const
// {
// 	return QString("git -C %1 %2").arg(this->_gitDir).arg(args);
// }