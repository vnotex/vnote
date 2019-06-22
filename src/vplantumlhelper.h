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

    static bool testPlantUMLJar(const QString &p_jar, QString &p_msg);

    static QByteArray process(const QString &p_format, const QString &p_text);

    static QString keywordForSmartLivePreview(const QString &p_text,
                                              QString &p_hints,
                                              bool &p_isRegex);

signals:
    void resultReady(int p_id,
                     TimeStamp p_timeStamp,
                     const QString &p_format,
                     const QString &p_result);

private slots:
    void handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus);

private:
    VPlantUMLHelper(const QString &p_jar, QObject *p_parent = nullptr);

    void prepareCommand(QString &p_cmd, QStringList &p_args, const QString &p_jar= QString()) const;

    static QStringList refineArgsForUse(const QStringList &p_args);

    QString m_program;

    QStringList m_args;

    // When not empty, @m_program and @m_args will be ignored.
    QString m_customCmd;
};

#endif // VPLANTUMLHELPER_H
