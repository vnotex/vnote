#ifndef _V_GIT_H_
#define _V_GIT_H_
#include <qstring.h>
#include <QObject>
#include <qprocess.h>

class QMessageBox;
class QPushButton;
class VGit : public QObject
{
	Q_OBJECT
private:
	enum class GitType
	{
		None,
		Status,
		Add,
		Commit,
		Push,
		Pull,
		Authentication
	};

	enum class GitTarget
	{
		Upload,
		Download,
	};

public:
	VGit(QWidget *parent = NULL);
	~VGit();
	void setGitDir(const QString &dir);
	void upload();
	void download();
private slots:
	void onReadOutput();
	void onReadError();
	void onProcessFinish(int exitCode);

private:
	void status();
	void add();
	void commit();
	void push();
	void pull();
	void authentication();
	void processDownload();
	void processUpload();

	void clear();
	void start(const QString &cmd);
	void showMessageBox(const QString &message, bool showButton);
	void hideMessageBox();
	void onMessageButtonClick();
	QString VGit::getGitHead(const QString &args) const;

private:
	QString _gitDir;

	QMessageBox *_messageBox;
	QPushButton *_messageButton;
	QProcess *_process;
	GitType _type;
	GitTarget _target;
	QString _output;
	QString _error;
};

inline void VGit::setGitDir(const QString &dir)
{
	this->_gitDir = dir;
};

inline QString VGit::getGitHead(const QString &args) const
{
	return QString("git -C %1 %2").arg(this->_gitDir).arg(args);
}

#endif
