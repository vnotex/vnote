#ifndef _V_GIT_H_
#define _V_GIT_H_
#include <qstring.h>
#include <QObject>

// class QProcess;
// class VGit : public QObject
// {
// 	Q_OBJECT
// public:
// 	enum class GitType
// 	{
// 		None,
// 		Status,
// 		Add,
// 		Commit,
// 		Push,
// 		Pull
// 	};
// public:
// 	VGit(const QString& gitDir);
// 	~VGit();
// 	void status();
// 	void add();
// 	void commit();
// 	void push();
// 	void pull();

// private:
// 	void onReadOutput();
// 	void onReadError();
// 	void clear();
// 	QString getGitHead(const QString& args)const;
// private:
// 	QString _gitDir;

// 	QProcess* _process;
// 	GitType _currentType;
// 	bool _success;
// 	QString _output;
// 	QString _error;
// };

class VGit
{
public:
	VGit(const QString& gitDir) : 
		_gitDir(gitDir)
	{
	}
	static VGit* create(const QString& gitDir, QString& output, QString& error);
	void Upload();
	void Download();
	bool isSuccess() const;
	const QString& getStandardOutput() const;
	const QString& getStandardError() const;
private:
	enum class GitType
	{
		None,
		Status,
		Add,
		Commit,
		Push,
		Pull
	};
	static void execute(const QString& cmd, QString& output, QString& error);
	static QString getGitString(const QString& gitDir, GitType type, const QString& arg = NULL);
private:
	QString _gitDir;
	QString _output;
	QString _error;
};

#endif
