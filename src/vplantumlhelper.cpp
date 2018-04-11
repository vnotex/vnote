#include "vplantumlhelper.h"

#include <QDebug>
#include <QThread>

#include "vconfigmanager.h"

extern VConfigManager *g_config;

#define TaskIdProperty "PlantUMLTaskId"
#define TaskFormatProperty "PlantUMLTaskFormat"
#define TaskTimeStampProperty "PlantUMLTaskTimeStamp"

VPlantUMLHelper::VPlantUMLHelper(QObject *p_parent)
    : QObject(p_parent)
{
    prepareCommand(m_program, m_args);
}

void VPlantUMLHelper::processAsync(int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QString &p_text)
{
    QProcess *process = new QProcess(this);
    process->setProperty(TaskIdProperty, p_id);
    process->setProperty(TaskTimeStampProperty, p_timeStamp);
    process->setProperty(TaskFormatProperty, p_format);
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(handleProcessFinished(int, QProcess::ExitStatus)));

    QStringList args(m_args);
    args << ("-t" + p_format);

    qDebug() << m_program << args;

    process->start(m_program, args);
    process->write(p_text.toUtf8());
    process->closeWriteChannel();
}

void VPlantUMLHelper::prepareCommand(QString &p_program, QStringList &p_args) const
{
    p_program = "java";

    p_args << "-jar" << g_config->getPlantUMLJar();
    p_args << "-charset" << "UTF-8";

    int nbthread = QThread::idealThreadCount();
    p_args << "-nbthread" << QString::number(nbthread > 0 ? nbthread : 1);

    const QString &dot = g_config->getGraphvizDot();
    if (!dot.isEmpty()) {
        p_args << "-graphvizdot";
        p_args << dot;
    }

    p_args << "-pipe";
}

void VPlantUMLHelper::handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    QProcess *process = static_cast<QProcess *>(sender());
    int id = process->property(TaskIdProperty).toInt();
    QString format = process->property(TaskFormatProperty).toString();
    TimeStamp timeStamp = process->property(TaskTimeStampProperty).toULongLong();
    qDebug() << "process finished" << id << timeStamp << format << p_exitCode << p_exitStatus;
    bool failed = true;
    if (p_exitStatus == QProcess::NormalExit) {
        if (p_exitCode < 0) {
            qWarning() << "PlantUML fail" << p_exitCode;
        } else {
            failed = false;
            QByteArray outBa = process->readAllStandardOutput();
            if (format == "svg") {
                emit resultReady(id, timeStamp, format, QString::fromLocal8Bit(outBa));
            } else {
                emit resultReady(id, timeStamp, format, QString::fromLocal8Bit(outBa.toBase64()));
            }
        }
    } else {
        qWarning() << "fail to start PlantUML process" << p_exitCode << p_exitStatus;
    }

    if (failed) {
        QByteArray errBa = process->readAllStandardError();
        if (!errBa.isEmpty()) {
            QString errStr(QString::fromLocal8Bit(errBa));
            qWarning() << "PlantUML stderr:" << errStr;
        }

        emit resultReady(id, timeStamp, format, "");
    }

    process->deleteLater();
}
