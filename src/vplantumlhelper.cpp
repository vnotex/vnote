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

static bool tryKeywords(QString &p_keyword)
{
    // start, stop, end, endif, repeat, fork, fork again, end fork, },
    // detach, end note, end box, endrnote, endhnote,
    // top to bottom direction, left to right direction,
    // @startuml, @enduml
    static QRegExp keywords("^\\s*(?:start|stop|end|endif|repeat|"
                            "fork(?:\\s+again)?|end\\s+fork|\\}|detach|"
                            "end ?(?:note|box)|endrnote|endhnote|"
                            "top\\s+to\\s+bottom\\s+direction|"
                            "left\\s+to\\s+right\\s+direction|"
                            "@startuml|@enduml)\\s*$");
    if (keywords.indexIn(p_keyword) >= 0) {
        p_keyword.clear();
        return true;
    }

    return false;
}

static bool tryClassDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex, bool &p_needCreole)
{
    Q_UNUSED(p_isRegex);

    // class ABC #Pink
    static QRegExp classDef1("^\\s*(?:class|(?:abstract(?:\\s+class)?)|interface|annotation|enum)\\s+"
                             "(?!class)(\\w+)");
    if (classDef1.indexIn(p_keyword) >= 0) {
        p_keyword = classDef1.cap(1);
        p_hints = "id";
        return true;
    }

    // class "ABC DEF" as AD #Pink
    static QRegExp classDef2("^\\s*(?:class|(?:abstract(?:\\s+class)?)|interface|annotation|enum)\\s+"
                             "\"([^\"]+)\"\\s*(?:\\bas\\s+(\\w+))?");
    if (classDef2.indexIn(p_keyword) >= 0) {
        if (classDef2.cap(2).isEmpty()) {
            p_keyword = classDef2.cap(1).trimmed();
        } else {
            p_keyword = classDef2.cap(2);
        }
        p_hints = "id";
        return true;
    }

    // class01 "1" *-- "many" class02 : contains 4 >
    static QRegExp relation("^\\s*(?:(\\w+)|\"([^\"]+)\")\\s*"
                            "(?:\"[^\"]+\"\\s*)?"
                            "(?:<\\||[*o<#x}+^])?" "(?:-+|\\.+)" "(?:\\|>|[*o>#x{+^])?\\s*"
                            "(?:\"[^\"]+\"\\s*)?"
                            "(?:(\\w+)|\"([^\"]+)\")\\s*"
                            "(?::(.+))?");
    if (relation.indexIn(p_keyword) >= 0) {
        if (relation.cap(5).isEmpty()) {
            QString class2 = relation.cap(3);
            if (class2.isEmpty()) {
                class2 = relation.cap(4).trimmed();
            }

            p_keyword = class2;
            p_hints = "id";
        } else {
            p_needCreole = true;
            p_keyword = relation.cap(5).trimmed();
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

    // note left on link #Pink : message
    // note left on link
    // node on link: message
    // MUST before next rule "note".
    static QRegExp note4("^\\s*note\\s+(?:(?:left|top|right|bottom)\\s+)?"
                         "on\\s+link"
                         "[^:]*"
                         "(?::(.*))?");
    if (note4.indexIn(p_keyword) >= 0) {
        p_needCreole = true;
        p_keyword = note4.cap(1).trimmed();
        return true;
    }

    // note top of Object #Pink : message
    // note top of Object
    // note top: message
    // hnote and rnote for sequence diagram.
    // note right of (use case)
    static QRegExp note("^\\s*[hr]?note\\s+(?:left|top|right|bottom)"
                        "(?:\\s+of\\s+(?:(\\w+)|\\(([^\\)]+)\\)))?"
                        "[^:]*"
                        "(?::(.*))?");
    if (note.indexIn(p_keyword) >= 0) {
        p_keyword = note.cap(3).trimmed();
        if (p_keyword.isEmpty()) {
            p_keyword = note.cap(2).trimmed();
            if (p_keyword.isEmpty()) {
                p_keyword = note.cap(1);
                p_hints = "id";
            } else {
                p_needCreole = true;
            }
        } else {
            p_needCreole = true;
        }

        return true;
    }

    // note "a floating note" as N1 #Pink
    // note as N1
    static QRegExp note2("^\\s*note\\s+(?:\"([^\"]*)\"\\s+)?as\\s+\\w+");
    if (note2.indexIn(p_keyword) >= 0) {
        p_keyword = note2.cap(1).trimmed();
        return true;
    }

    return false;
}

static bool tryCommonElements(QString &p_keyword, QString &p_hints, bool &p_isRegex, bool &p_needCreole)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);
    Q_UNUSED(p_needCreole);

    // Words in quotes.
    // cmf("abc")
    static QRegExp quote("^[^\"]*\"([^\"]+)\"[^\"]*$");
    if (quote.indexIn(p_keyword) >= 0) {
        p_keyword = quote.cap(1).trimmed();
        return true;
    }

    return false;
}

static bool tryActivityDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex, bool &p_needCreole)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);

    // Activity. (Do not support color.)
    // :Hello world;
    // :Across multiple lines
    static QRegExp activity1("^\\s*:([^:]+)\\s*$");
    if (activity1.indexIn(p_keyword) >= 0) {
        p_keyword = activity1.cap(1).trimmed();
        if (!p_keyword.isEmpty()) {
            QChar ch = p_keyword[p_keyword.size() - 1];
            if (ch == ';' || ch == '|' || ch == '<' || ch == '>'
                || ch == '/' || ch == ']' || ch == '}') {
                p_keyword = p_keyword.left(p_keyword.size() - 1);
            }
        }

        p_needCreole = true;
        return true;
    }

    // Activity.
    // multiple lines;
    static QRegExp activity2("^\\s*(.+)([;|<>/\\]}])\\s*$");
    if (activity2.indexIn(p_keyword) >= 0) {
        QString word = activity2.cap(1);
        QChar end = activity2.cap(2)[0];
        if (end != ';' && !word.isEmpty()) {
            // || << >> // ]] }} are not legal.
            if (word[word.size() - 1] == end) {
                return false;
            }
        }

        // It may conflict with note.
        p_needCreole = true;
        p_keyword = word.trimmed();
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

static bool trySequenceDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex, bool &p_needCreole)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);

    // participant ABC #Pink
    // participant "ABC DEF" as AD #Pink
    static QRegExp participant1("^\\s*(?:participant|actor|boundary|control|entity|database)\\s+"
                                "(?:(\\w+)|\"([^\"]+)\"\\s*(?:\\bas\\s+\\w+)?)");
    if (participant1.indexIn(p_keyword) >= 0) {
        p_keyword = participant1.cap(1);
        if (p_keyword.isEmpty()) {
            p_keyword = participant1.cap(2).trimmed();
        }

        return true;
    }

    // "abc" ->> "def" : Authentication
    static QRegExp message("^\\s*(?:\\w+|\"[^\"]+\")\\s+"
                           "[-<>x\\\\/o]{2,}\\s+"
                           "(?:\\w+|\"[^\"]+\")\\s*"
                           ":\\s*(.+)");
    if (message.indexIn(p_keyword) >= 0) {
        p_needCreole = true;
        p_keyword = message.cap(1).trimmed();
        return true;
    }

    // autonumber
    static QRegExp autonum("^\\s*autonumber\\s+");
    if (autonum.indexIn(p_keyword) >= 0) {
        p_keyword.clear();
        return true;
    }

    // newpage
    static QRegExp newpage("^\\s*newpage\\s+(.+)");
    if (newpage.indexIn(p_keyword) >= 0) {
        p_keyword = newpage.cap(1).trimmed();
        return true;
    }

    // alt, else, group, loop ABCDEFG
    static QRegExp group1("^\\s*(?:alt|else|group|loop)\\s+(.*)");
    if (group1.indexIn(p_keyword) >= 0) {
        p_keyword = group1.cap(1).trimmed();
        return true;
    }

    // note over bob, alice #Pink:
    // ref over bob, alice : init
    static QRegExp noteon("^\\s*(?:[hr]?note|ref)\\s+over\\s+"
                          "(\\w+)[^:]*"
                          "(?::(.+))?");
    if (noteon.indexIn(p_keyword) >= 0) {
        p_keyword = noteon.cap(2).trimmed();
        if (p_keyword.isEmpty()) {
            p_keyword = noteon.cap(1);
        } else {
            p_needCreole = true;
        }

        return true;
    }

    // Divider.
    // == Initialization ==
    static QRegExp divider("^\\s*==\\s*([^=]*)==\\s*$");
    if (divider.indexIn(p_keyword) >= 0) {
        p_keyword = divider.cap(1).trimmed();
        return true;
    }

    // Delay.
    // ... 5 minutes latter ...
    static QRegExp delay("^\\s*\\.\\.\\.(?:(.+)\\.\\.\\.)?\\s*$");
    if (delay.indexIn(p_keyword) >= 0) {
        p_keyword = delay.cap(1).trimmed();
        return true;
    }

    // activate A
    static QRegExp activate("^\\s*(?:(?:de)?activate|destroy)\\s+"
                            "(?:(\\w+)|\"([^\"]+)\")");
    if (activate.indexIn(p_keyword) >= 0) {
        p_keyword = activate.cap(1);
        if (p_keyword.isEmpty()) {
            p_keyword = activate.cap(2).trimmed();
        }

        return true;
    }

    // create control ABC
    static QRegExp create("^\\s*create\\s+(?:\\w+\\s+)?"
                          "(?:(\\w+)|\"([^\"]+)\")");
    if (create.indexIn(p_keyword) >= 0) {
        p_keyword = create.cap(1);
        if (p_keyword.isEmpty()) {
            p_keyword = create.cap(2).trimmed();
        }

        return true;
    }

    // Incoming and outgoing message.
    static QRegExp incoming("^\\s*\\[[-<>ox]+\\s*"
                            "(?:\\w+|\"[^\"]+\")\\s*"
                            ":\\s*(.+)");
    if (incoming.indexIn(p_keyword) >= 0) {
        p_needCreole = true;
        p_keyword = incoming.cap(1).trimmed();
        return true;
    }

    static QRegExp outgoing("^\\s*(?:\\w+|\"[^\"]+\")\\s*"
                            "[-<>ox]+\\]\\s*"
                            ":\\s*(.+)");
    if (outgoing.indexIn(p_keyword) >= 0) {
        p_needCreole = true;
        p_keyword = outgoing.cap(1).trimmed();
        return true;
    }

    // box "Internal Service" #Pink
    static QRegExp box("^\\s*box(?:\\s+\"([^\"]+)\")?\\s*");
    if (box.indexIn(p_keyword) >= 0) {
        p_keyword = box.cap(1).trimmed();
        return true;
    }

    return false;
}

static bool tryUseCaseDiagram(QString &p_keyword, QString &p_hints, bool &p_isRegex, bool &p_needCreole)
{
    Q_UNUSED(p_isRegex);
    Q_UNUSED(p_hints);

    // User -> (Start)
    // User --> (Use the application) : A small label
    // :Main Admin: --> (Use the application) : This is another label
    // (chekckout) -- (payment) : include
    static QRegExp rel("^\\s*(?:(\\w+)|:([^:]+):|\\(([^\\)]+)\\))\\s*"
                       "[-.<>]{2,}\\s*"
                       "(?:\\(([^\\)]+)\\)|(\\w+))\\s*"
                       "(?::(.+))?");
    if (rel.indexIn(p_keyword) >= 0) {
        QString msg(rel.cap(6).trimmed());
        if (msg.isEmpty()) {
            QString ent2(rel.cap(4).trimmed());
            if (ent2.isEmpty()) {
                ent2 = rel.cap(5).trimmed();
            }

            if (ent2.isEmpty()) {
                QString ent1(rel.cap(3).trimmed());
                if (ent1.isEmpty()) {
                    ent1 = rel.cap(2).trimmed();
                    if (ent1.isEmpty()) {
                        ent1 = rel.cap(1).trimmed();
                    }
                }

                p_keyword = ent1;
            } else {
                p_keyword = ent2;
            }
        } else {
            p_keyword = msg;
        }

        p_needCreole = true;
        return true;
    }

    // Usecase.
    // (First usecase) as (UC2)
    // usecase UC3
    // usecase (Last usecase) as UC4
    static QRegExp usecase1("^\\s*usecase\\s+(?:(\\w+)|\\(([^\\)]+)\\)\\s+as\\s+\\w+)");
    if (usecase1.indexIn(p_keyword) >= 0) {
        if (usecase1.cap(1).isEmpty()) {
            p_keyword = usecase1.cap(2).trimmed();
            p_needCreole = true;
        } else {
            p_keyword = usecase1.cap(1);
        }

        return true;
    }

    // This will eat almost anything starting with ().
    static QRegExp usecase2("^\\s*\\(([^\\)]+)\\)");
    if (usecase2.indexIn(p_keyword) >= 0) {
        p_keyword = usecase2.cap(1).trimmed();
        p_needCreole = true;
        return true;
    }

    // Actor.
    // :Another\nactor: as men2
    // actor :Last actor: as men4
    static QRegExp actor1("^\\s*(?:actor\\s+)?:([^:]+):(?:\\s+as\\s+\\w+)?[^:]*$");
    if (actor1.indexIn(p_keyword) >= 0) {
        p_keyword = actor1.cap(1).trimmed();
        p_needCreole = true;
        return true;
    }

    // actor men1
    static QRegExp actor2("^\\s*actor\\s+(\\w+)");
    if (actor2.indexIn(p_keyword) >= 0) {
        p_keyword = actor2.cap(1).trimmed();
        p_needCreole = true;
        return true;
    }

    // rectangle checkout {
    static QRegExp rect("^\\s*rectangle\\s+(?:(\\w+)|\"([^\"]+)\")");
    if (rect.indexIn(p_keyword) >= 0) {
        p_keyword = rect.cap(1).trimmed();
        if (p_keyword.isEmpty()) {
            p_keyword = rect.cap(2).trimmed();
        }

        p_needCreole = true;
        return true;
    }

    return false;
}

static bool tryCreole(QString &p_keyword)
{
    if (p_keyword.isEmpty()) {
        return false;
    }

    // List.
    // ** list
    // # list
    static QRegExp listMark("^\\s*(?:\\*+|#+)\\s+(.+)$");
    if (listMark.indexIn(p_keyword) >= 0) {
        p_keyword = listMark.cap(1).trimmed();
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
    bool needCreole = false;

    qDebug() << "tryKeywords" << kw;

    if (tryKeywords(kw)) {
        return kw;
    }

    qDebug() << "tryClassDiagram" << kw;

    if (tryClassDiagram(kw, p_hints, p_isRegex, needCreole)) {
        if (needCreole) {
            goto creole;
        }

        return kw;
    }

    qDebug() << "tryActivityDiagram" << kw;

    if (tryActivityDiagram(kw, p_hints, p_isRegex, needCreole)) {
        if (needCreole) {
            goto creole;
        }

        return kw;
    }

    qDebug() << "trySequenceDiagram" << kw;

    if (trySequenceDiagram(kw, p_hints, p_isRegex, needCreole)) {
        if (needCreole) {
            goto creole;
        }

        return kw;
    }

    qDebug() << "tryUseCaseDiagram" << kw;

    if (tryUseCaseDiagram(kw, p_hints, p_isRegex, needCreole)) {
        if (needCreole) {
            goto creole;
        }

        return kw;
    }

    qDebug() << "tryCommonElements" << kw;

    if (tryCommonElements(kw, p_hints, p_isRegex, needCreole)) {
        if (needCreole) {
            goto creole;
        }

        return kw;
    }

creole:
    tryCreole(kw);
    return kw;
}
