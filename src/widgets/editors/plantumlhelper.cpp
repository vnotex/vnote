#include "plantumlhelper.h"

#include <QDebug>

#include <utils/processutils.h>

using namespace vnotex;

void PlantUmlHelper::init(const QString &p_plantUmlJarFile,
                          const QString &p_graphvizFile,
                          const QString &p_overriddenCommand)
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    update(p_plantUmlJarFile, p_graphvizFile, p_overriddenCommand);
}

void PlantUmlHelper::update(const QString &p_plantUmlJarFile,
                            const QString &p_graphvizFile,
                            const QString &p_overriddenCommand)
{
    if (!m_initialized) {
        return;
    }

    m_overriddenCommand = p_overriddenCommand;
    if (m_overriddenCommand.isEmpty()) {
        prepareProgramAndArgs(p_plantUmlJarFile, p_graphvizFile, m_program, m_args);
    } else {
        m_program.clear();
        m_args.clear();
    }

    checkValidProgram();

    clearCache();
}

void PlantUmlHelper::prepareProgramAndArgs(const QString &p_plantUmlJarFile,
                                           const QString &p_graphvizFile,
                                           QString &p_program,
                                           QStringList &p_args)
{
    p_program.clear();
    p_args.clear();

#if defined(Q_OS_WIN)
    p_program = "java";
#else
    p_program = "/bin/sh";
    p_args << "-c";
    p_args << "java";
#endif

    p_args << "-Djava.awt.headless=true";

    p_args << "-jar" << p_plantUmlJarFile;

    p_args << "-charset" << "UTF-8";

    if (!p_graphvizFile.isEmpty()) {
        p_args << "-graphvizdot" << p_graphvizFile;
    }

    p_args << "-pipe";
}

QPair<bool, QString> PlantUmlHelper::testPlantUml(const QString &p_plantUmlJarFile)
{
    auto ret = qMakePair(false, QString());

    QString program;
    QStringList args;
    prepareProgramAndArgs(p_plantUmlJarFile, QString(), program, args);

    args << "-tsvg";
    args = getArgsToUse(args);

    const QString testGraph("VNote->Markdown : Hello");

    int exitCode = -1;
    QByteArray outData;
    QByteArray errData;
    auto state = ProcessUtils::start(program,
                                     args,
                                     testGraph.toUtf8(),
                                     exitCode,
                                     outData,
                                     errData);
    ret.first = (state == ProcessUtils::Succeeded) && (exitCode == 0);

    ret.second = QString("%1 %2\n\nExitcode: %3\n\nOutput: %4\n\nError: %5")
                        .arg(program, args.join(' '), QString::number(exitCode), QString::fromLocal8Bit(outData), QString::fromLocal8Bit(errData));

    return ret;
}

QStringList PlantUmlHelper::getFormatArgs(const QString &p_format)
{
    QStringList args;
    args << ("-t" + p_format);
    return args;
}
