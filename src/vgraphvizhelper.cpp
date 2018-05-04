#include "vgraphvizhelper.h"

#include <QDebug>
#include <QThread>

#include "vconfigmanager.h"

extern VConfigManager *g_config;

#define TaskIdProperty "GraphvizTaskId"
#define TaskFormatProperty "GraphvizTaskFormat"
#define TaskTimeStampProperty "GraphvizTaskTimeStamp"

VGraphvizHelper::VGraphvizHelper(QObject *p_parent)
    : QObject(p_parent)
{
    prepareCommand(m_program, m_args);
}

void VGraphvizHelper::processAsync(int p_id, TimeStamp p_timeStamp, const QString &p_format, const QString &p_text)
{
    QProcess *process = new QProcess(this);
    process->setProperty(TaskIdProperty, p_id);
    process->setProperty(TaskTimeStampProperty, p_timeStamp);
    process->setProperty(TaskFormatProperty, p_format);
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(handleProcessFinished(int, QProcess::ExitStatus)));

    QStringList args(m_args);
    args << ("-T" + p_format);

    qDebug() << m_program << args;

    process->start(m_program, args);
    if (process->write(p_text.toUtf8()) == -1) {
        qWarning() << "fail to write to QProcess:" << process->errorString();
    }

    process->closeWriteChannel();
}

void VGraphvizHelper::prepareCommand(QString &p_program, QStringList &p_args) const
{
    const QString &dot = g_config->getGraphvizDot();
    if (dot.isEmpty()) {
        p_program = "dot";
    } else {
        p_program = dot;
    }

    p_args.clear();
}

void VGraphvizHelper::handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    QProcess *process = static_cast<QProcess *>(sender());
    int id = process->property(TaskIdProperty).toInt();
    QString format = process->property(TaskFormatProperty).toString();
    TimeStamp timeStamp = process->property(TaskTimeStampProperty).toULongLong();
    qDebug() << QString("Graphviz finished: id %1 timestamp %2 format %3 exitcode %4 exitstatus %5")
                       .arg(id)
                       .arg(timeStamp)
                       .arg(format)
                       .arg(p_exitCode)
                       .arg(p_exitStatus);
    bool failed = true;
    if (p_exitStatus == QProcess::NormalExit) {
        if (p_exitCode < 0) {
            qWarning() << "Graphviz fail" << p_exitCode;
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
        qWarning() << "fail to start Graphviz process" << p_exitCode << p_exitStatus;
    }

    QByteArray errBa = process->readAllStandardError();
    if (!errBa.isEmpty()) {
        QString errStr(QString::fromLocal8Bit(errBa));
        if (failed) {
            qWarning() << "Graphviz stderr:" << errStr;
        } else {
            qDebug() << "Graphviz stderr:" << errStr;
        }
    }

    if (failed) {
        emit resultReady(id, timeStamp, format, "");
    }

    process->deleteLater();
}
