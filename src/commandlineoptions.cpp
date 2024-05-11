#include "commandlineoptions.h"

#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QDebug>

#include <widgets/mainwindow.h>

using vnotex::MainWindow;

CommandLineOptions::ParseResult CommandLineOptions::parse(const QStringList &p_arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(MainWindow::tr("A pleasant note-taking platform."));
    const auto helpOpt = parser.addHelpOption();
    const auto versionOpt = parser.addVersionOption();

    // Positional arguments.
    parser.addPositionalArgument("paths", MainWindow::tr("Files or folders to open."));

    const QCommandLineOption verboseOpt("verbose", MainWindow::tr("Print more logs."));
    parser.addOption(verboseOpt);

    const QCommandLineOption logStderrOpt("log-stderr", MainWindow::tr("Log to stderr."));
    parser.addOption(logStderrOpt);

    // WebEngine options.
    // No need to handle them. Just add them to the parser to avoid parse error.
    {
        QCommandLineOption webRemoteDebuggingPortOpt("remote-debugging-port",
                                                     MainWindow::tr("WebEngine remote debugging port."),
                                                     MainWindow::tr("port_number"));
        webRemoteDebuggingPortOpt.setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(webRemoteDebuggingPortOpt);

        QCommandLineOption webNoSandboxOpt("no-sandbox", MainWindow::tr("WebEngine without sandbox."));
        webNoSandboxOpt.setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(webNoSandboxOpt);

        QCommandLineOption webDisableGpu("disable-gpu", MainWindow::tr("WebEngine with GPU disabled."));
        webDisableGpu.setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(webDisableGpu);
    }

    if (!parser.parse(p_arguments)) {
        m_errorMsg = parser.errorText();
        return ParseResult::Error;
    }

    // Handle results.
    m_helpText = parser.helpText();
    if (parser.isSet(helpOpt)) {
        return ParseResult::HelpRequested;
    }

    if (parser.isSet(versionOpt)) {
        return ParseResult::VersionRequested;
    }

    // Position arguments.
    const auto args = parser.positionalArguments();
    m_pathsToOpen = args;

    if (parser.isSet(verboseOpt)) {
        m_verbose = true;
    }

    if (parser.isSet(logStderrOpt)) {
        m_logToStderr = true;
    }

    return ParseResult::Ok;
}
