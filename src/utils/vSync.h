#ifndef _V_SYNC_H_
#define _V_SYNC_H_
#include <qstring.h>
#include <QObject>
#include <qprocess.h>

class QMessageBox;
class QPushButton;
class VSync : public QObject
{
    Q_OBJECT
private:
    enum class SyncType
    {
        None,
        Status,
        Add,
        Commit,
        Push,
        Pull,
        Authentication
    };

    enum class SyncTarget
    {
        None,
        Upload,
        Download,
    };
signals:
    void downloadSuccess();
    void uploadSuccess();
public:
    VSync(QWidget *parent = NULL);
    ~VSync();
    void setDir(const QString &dir);
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
    void downloadFinish();
    void uploadFinish();

    void start(const QString &cmd);
    void showMessageBox(const QString &message, bool showButton);
    void hideMessageBox();
    void onMessageButtonClick();
    QString getSyncHead(const QString &args) const;

private:
    QString m_dir;

    QMessageBox *m_messageBox;
    QPushButton *m_messageButton;
    QProcess *m_process;
    SyncType m_type;
    SyncTarget m_target;
    QString m_output;
    QString m_error;
};

inline void VSync::setDir(const QString &dir)
{
    this->m_dir = dir;
};

inline QString VSync::getSyncHead(const QString &args) const
{
    return QString("git -C %1 %2").arg(this->m_dir).arg(args);
}

#endif
