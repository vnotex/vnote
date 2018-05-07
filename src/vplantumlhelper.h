#ifndef VPLANTUMLHELPER_H
#define VPLANTUMLHELPER_H

#include <QObject>

#include <QStringList>
#include <QProcess>

#include "vconstants.h"

class VPlantUMLHelper : public QObject
{
    Q_OBJECT
public:
    explicit VPlantUMLHelper(QObject *p_parent = nullptr);

    void processAsync(int p_id,
                      TimeStamp p_timeStamp,
                      const QString &p_format,
                      const QString &p_text);

    void prepareCommand(QString &p_customCmd, QString &p_cmd, QStringList &p_args) const;

signals:
    void resultReady(int p_id, TimeStamp p_timeStamp, const QString &p_format, const QString &p_result);

private slots:
    void handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus);

private:
    QString m_program;

    QStringList m_args;

    // When not empty, @m_program and @m_args will be ignored.
    QString m_customCmd;
};

#endif // VPLANTUMLHELPER_H
