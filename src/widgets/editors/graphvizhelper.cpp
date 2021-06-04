#include "graphvizhelper.h"

#include <QDebug>

#include <utils/processutils.h>

using namespace vnotex;

void GraphvizHelper::init(const QString &p_graphvizFile)
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    update(p_graphvizFile);
}

void GraphvizHelper::update(const QString &p_graphvizFile)
{
    if (!m_initialized) {
        return;
    }

    prepareProgramAndArgs(p_graphvizFile, m_program, m_args);

    checkValidProgram();

    clearCache();
}

void GraphvizHelper::prepareProgramAndArgs(const QString &p_graphvizFile,
                                           QString &p_program,
                                           QStringList &p_args)
{
    p_program.clear();
    p_args.clear();

    p_program = p_graphvizFile.isEmpty() ? QStringLiteral("dot") : p_graphvizFile;
}

QPair<bool, QString> GraphvizHelper::testGraphviz(const QString &p_graphvizFile)
{
    auto ret = qMakePair(false, QString());

    QString program;
    QStringList args;
    prepareProgramAndArgs(p_graphvizFile, program, args);
    args << "-Tsvg";

    const QString testGraph("digraph G {VNote->Markdown}");

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

QStringList GraphvizHelper::getFormatArgs(const QString &p_format)
{
    QStringList args;
    args << ("-T" + p_format);
    return args;
}
