#include "vplantumlhelper.h"

#include <QDebug>
#include <QThread>

#include "vconfigmanager.h"
#include "utils/vprocessutils.h"

extern VConfigManager *g_config;

#define TaskIdProperty "PlantUMLTaskId"
#define TaskFormatProperty "PlantUMLTaskFormat"
#define TaskTimeStampProperty "PlantUMLTaskTimeStamp"

VPlantUMLHelper::VPlantUMLHelper(QObject *p_parent)
    : QObject(p_parent)
{
    m_customCmd = g_config->getPlantUMLCmd();
    if (m_customCmd.isEmpty()) {
        prepareCommand(m_program, m_args);
    }
}

VPlantUMLHelper::VPlantUMLHelper(const QString &p_jar, QObject *p_parent)
    : QObject(p_parent)
{
    m_customCmd = g_config->getPlantUMLCmd();
    if (m_customCmd.isEmpty()) {
        prepareCommand(m_program, m_args, p_jar);
    }
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

    if (m_customCmd.isEmpty()) {
        QStringList args(m_args);
        args << ("-t" + p_format);
        qDebug() << m_program << args;
        process->start(m_program, args);
    } else {
        QString cmd(m_customCmd);
        cmd.replace("%0", p_format);
        qDebug() << cmd;
        process->start(cmd);
    }

    if (process->write(p_text.toUtf8()) == -1) {
        qWarning() << "fail to write to QProcess:" << process->errorString();
    }

    process->closeWriteChannel();
}

void VPlantUMLHelper::prepareCommand(QString &p_program,
                                     QStringList &p_args,
                                     const QString &p_jar) const
{
    p_program = "java";

    p_args << "-jar" << (p_jar.isEmpty() ? g_config->getPlantUMLJar() : p_jar);
    p_args << "-charset" << "UTF-8";

    int nbthread = QThread::idealThreadCount();
    p_args << "-nbthread" << QString::number(nbthread > 0 ? nbthread : 1);

    const QString &dot = g_config->getGraphvizDot();
    if (!dot.isEmpty()) {
        p_args << "-graphvizdot";
        p_args << dot;
    }

    p_args << "-pipe";
    p_args << g_config->getPlantUMLArgs();
}

void VPlantUMLHelper::handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    QProcess *process = static_cast<QProcess *>(sender());
    int id = process->property(TaskIdProperty).toInt();
    QString format = process->property(TaskFormatProperty).toString();
    TimeStamp timeStamp = process->property(TaskTimeStampProperty).toULongLong();
    qDebug() << QString("PlantUML finished: id %1 timestamp %2 format %3 exitcode %4 exitstatus %5")
                       .arg(id)
                       .arg(timeStamp)
                       .arg(format)
                       .arg(p_exitCode)
                       .arg(p_exitStatus);
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

    QByteArray errBa = process->readAllStandardError();
    if (!errBa.isEmpty()) {
        QString errStr(QString::fromLocal8Bit(errBa));
        if (failed) {
            qWarning() << "PlantUML stderr:" << errStr;
        } else {
            qDebug() << "PlantUML stderr:" << errStr;
        }
    }

    if (failed) {
        emit resultReady(id, timeStamp, format, "");
    }

    process->deleteLater();
}

bool VPlantUMLHelper::testPlantUMLJar(const QString &p_jar, QString &p_msg)
{
    VPlantUMLHelper inst(p_jar);
    QStringList args(inst.m_args);
    args << "-tsvg";

    QString testGraph("VNote->Markdown : hello");

    int exitCode = -1;
    QByteArray out, err;
    int ret = VProcessUtils::startProcess(inst.m_program,
                                          args,
                                          testGraph.toUtf8(),
                                          exitCode,
                                          out,
                                          err);

    p_msg = QString("Command: %1 %2\nExitCode: %3\nOutput: %4\nError: %5")
                   .arg(inst.m_program)
                   .arg(args.join(' '))
                   .arg(exitCode)
                   .arg(QString::fromLocal8Bit(out))
                   .arg(QString::fromLocal8Bit(err));

    return ret == 0 && exitCode == 0;
}

QByteArray VPlantUMLHelper::process(const QString &p_format, const QString &p_text)
{
    VPlantUMLHelper inst;

    int exitCode = -1;
    QByteArray out, err;
    int ret = -1;
    if (inst.m_customCmd.isEmpty()) {
        QStringList args(inst.m_args);
        args << ("-t" + p_format);
        ret = VProcessUtils::startProcess(inst.m_program,
                                          args,
                                          p_text.toUtf8(),
                                          exitCode,
                                          out,
                                          err);
    } else {
        QString cmd(inst.m_customCmd);
        cmd.replace("%0", p_format);
        ret = VProcessUtils::startProcess(cmd,
                                          p_text.toUtf8(),
                                          exitCode,
                                          out,
                                          err);
    }

    if (ret != 0 || exitCode < 0) {
        qWarning() << "PlantUML fail" << ret << exitCode << QString::fromLocal8Bit(err);
    }

    return out;
}

static bool tryClassDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex)
{
    Q_UNUSED(p_isRegex);

    // class ABC #Pink {
    static QRegExp classDef1("^\\s*(?:class|(?:abstract(?:\\s+class)?)|interface|annotation|enum)\\s*"
                             "(?!class)(\\w+)\\s*.*");
    if (classDef1.indexIn(p_keyword) >= 0) {
        p_keyword = classDef1.cap(1);
        p_hints = "id";
        return true;
    }

    // class "ABC DEF" as AD #Pink {
    static QRegExp classDef2("^\\s*(?:class|(?:abstract(?:\\s+class)?)|interface|annotation|enum)\\s*"
                             "\"([^\"]+)\"\\s*(?:\\bas (\\w+))?.*");
    if (classDef2.indexIn(p_keyword) >= 0) {
        if (classDef2.cap(2).isEmpty()) {
            p_keyword = classDef2.cap(1);
        } else {
            p_keyword = classDef2.cap(2);
        }
        p_hints = "id";
        return true;
    }

    // class01 "1" *-- "many" class02 : contains 4 >
    static QRegExp relation("^\\s*(?:(\\w+)|\"([^\"]+)\")\\s+"
                            "(?:\"[^\"]+\"\\s+)?"
                            "(?:<\\||[*o<#x}+^])?" "(?:-+|\\.+)" "(?:\\|>|[*o>#x{+^])?\\s+"
                            "(?:\"[^\"]+\"\\s+)?"
                            "(?:(\\w+)|\"([^\"]+)\")\\s*"
                            "(?::(.+))?");
    if (relation.indexIn(p_keyword) >= 0) {
        QString note(relation.cap(5));
        if (note.isEmpty()) {
            QString class2 = relation.cap(3);
            if (class2.isEmpty()) {
                class2 = relation.cap(4);
            }

            p_keyword = class2;
            p_hints = "id";
        } else {
            p_keyword = note.trimmed();
        }

        return true;
    }

    // {static} field : String
    bool containsModifier = false;
    static QRegExp modifier("\\{(?:static|abstract|classifier)\\}");
    if (modifier.indexIn(p_keyword) >= 0) {
        containsModifier = true;
        p_keyword.remove(modifier);
    }

    // + field
    static QRegExp member("^\\s*[-#~+]\\s*(.*)");
    if (member.indexIn(p_keyword) >= 0) {
        p_keyword = member.cap(1).trimmed();
        return true;
    } else if (containsModifier) {
        p_keyword = p_keyword.trimmed();
        return true;
    }

    // note left on link: message
    // note left on link
    // node on link: message
    // MUST before next rule "note".
    static QRegExp note4("^\\s*note\\s+(?:(?:left|top|right|bottom)\\s+)?"
                         "on\\s+link"
                         "[^:]*"
                         "(?::(.*))?");
    if (note4.indexIn(p_keyword) >= 0) {
        p_keyword = note4.cap(1).trimmed();
        return true;
    }

    // note top of Object: message
    // note top of Object
    // note top: message
    static QRegExp note("^\\s*note\\s+(?:left|top|right|bottom)"
                        "(?:\\s+of\\s+(\\w+))?\\s*"
                        "(?::(.*))?");
    if (note.indexIn(p_keyword) >= 0) {
        p_keyword = note.cap(2).trimmed();
        if (p_keyword.isEmpty()) {
            p_keyword = note.cap(1);
            if (!p_keyword.isEmpty()) {
                p_hints = "id";
            }
        }

        return true;
    }

    // note "a floating note" as N1
    // note as N1
    static QRegExp note2("^\\s*note\\s+(?:\"([^\"]*)\"\\s+)?as\\s+\\w+\\s*");
    if (note2.indexIn(p_keyword) >= 0) {
        p_keyword = note2.cap(1);
        return true;
    }

    // end note
    static QRegExp note3("^\\s*end note\\s*$");
    if (note3.indexIn(p_keyword) >= 0) {
        p_keyword.clear();
        return true;
    }

    return false;
}

static bool tryCommonElements(QString &p_keyword, QString &p_hints, bool &p_isRegex)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);

    // List.
    // ** list
    // # list
    static QRegExp listMark("^\\s*(?:\\*+|#+)\\s+(.+)$");
    if (listMark.indexIn(p_keyword) >= 0) {
        p_keyword = listMark.cap(1).trimmed();
        return true;
    }

    // Words in quotes.
    // cmf("abc")
    static QRegExp quote("^[^\"]*\"([^\"]+)\"[^\"]*$");
    if (quote.indexIn(p_keyword) >= 0) {
        p_keyword = quote.cap(1).trimmed();
        return true;
    }

    return false;
}

static bool tryActivityDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);

    // Activity. (Do not support color.)
    // :Hello world;
    // :Across multiple lines
    static QRegExp activity1("^\\s*:(.+)\\s*$");
    if (activity1.indexIn(p_keyword) >= 0) {
        p_keyword = activity1.cap(1).trimmed();
        if (!p_keyword.isEmpty()) {
            QChar ch = p_keyword[p_keyword.size() - 1];
            if (ch == ';' || ch == '|' || ch == '<' || ch == '>'
                || ch == '/' || ch == ']' || ch == '}') {
                p_keyword = p_keyword.left(p_keyword.size() - 1);
            }
        }

        return true;
    }

    // Activity.
    // multiple lines;
    static QRegExp activity2("^\\s*(.+)[;|<>/\\]}]\\s*$");
    if (activity2.indexIn(p_keyword) >= 0) {
        p_keyword = activity2.cap(1).trimmed();
        return true;
    }

    // start, stop, end, endif, repeat, fork, fork again, end fork, },
    // detach
    static QRegExp start("^\\s*(?:start|stop|end|endif|repeat|"
                         "fork(?:\\s+again)?|end\\s+fork|\\}|detach)\\s*$");
    if (start.indexIn(p_keyword) >= 0) {
        p_keyword.clear();
        return true;
    }

    // Conditionals.
    // if (Graphviz) then (yes)
    // else if (Graphviz) then (yes)
    static QRegExp conIf("^\\s*(?:else)?if\\s+\\(([^\\)]+)\\)\\s+then(?:\\s+\\([^\\)]+\\))?\\s*$");
    if (conIf.indexIn(p_keyword) >= 0) {
        p_keyword = conIf.cap(1).trimmed();
        return true;
    }

    // else (no)
    static QRegExp conElse("^\\s*else(?:\\s+\\(([^\\)]+)\\))?\\s*$");
    if (conElse.indexIn(p_keyword) >= 0) {
        p_keyword = conElse.cap(1).trimmed();
        return true;
    }

    // Repeat loop.
    // repeat while (more data?)
    static QRegExp repeat("^\\s*repeat\\s+while\\s+\\(([^\\)]+)\\)\\s*$");
    if (repeat.indexIn(p_keyword) >= 0) {
        p_keyword = repeat.cap(1).trimmed();
        return true;
    }

    // while (check?) is (not empty)
    static QRegExp whileLoop("^\\s*while\\s+\\(([^\\)]+)\\)(?:\\s+is\\s+\\([^\\)]+\\))?\\s*$");
    if (whileLoop.indexIn(p_keyword) >= 0) {
        p_keyword = whileLoop.cap(1).trimmed();
        return true;
    }

    // endwhile (empty)
    static QRegExp endWhile("^\\s*endwhile(?:\\s+\\(([^\\)]+)\\))?\\s*$");
    if (endWhile.indexIn(p_keyword) >= 0) {
        p_keyword = endWhile.cap(1).trimmed();
        return true;
    }

    // partition Running {
    static QRegExp partition("^\\s*partition\\s+(\\w+)\\s+\\{\\s*$");
    if (partition.indexIn(p_keyword) >= 0) {
        p_keyword = partition.cap(1).trimmed();
        return true;
    }

    // |Swimlane1|
    // |#Pink|Swimlane1|
    static QRegExp swimline("^\\s*(?:\\|[^\\|]+)?\\|([^|]+)\\|\\s*$");
    if (swimline.indexIn(p_keyword) >= 0) {
        p_keyword = swimline.cap(1).trimmed();
        return true;
    }

    return false;
}

QString VPlantUMLHelper::keywordForSmartLivePreview(const QString &p_text,
                                                    QString &p_hints,
                                                    bool &p_isRegex)
{
    QString kw = p_text.trimmed();
    if (kw.isEmpty()) {
        return kw;
    }

    p_isRegex = false;

    qDebug() << "tryClassDiagram" << kw;

    if (tryClassDiagram(kw, p_hints, p_isRegex)) {
        return kw;
    }

    qDebug() << "tryActivityDiagram" << kw;

    if (tryActivityDiagram(kw, p_hints, p_isRegex)) {
        return kw;
    }

    qDebug() << "tryCommonElements" << kw;

    if (tryCommonElements(kw, p_hints, p_isRegex)) {
        return kw;
    }

    return kw;
}
