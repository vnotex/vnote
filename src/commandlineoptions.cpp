#include "commandlineoptions.h"

#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QDebug>

#define TR(x) QCoreApplication::translate("main", (x))

CommandLineOptions::ParseResult CommandLineOptions::parse(const QStringList &p_arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(TR("A pleasant note-taking platform."));
    const auto helpOpt = parser.addHelpOption();
    const auto versionOpt = parser.addVersionOption();

    // Positional arguments.
    parser.addPositionalArgument("paths", TR("Files or folders to open."));

    const QCommandLineOption verboseOpt("verbose", TR("Print more logs."));
    parser.addOption(verboseOpt);

    // WebEngine options.
    // No need to handle them. Just add them to the parser to avoid parse error.
    QCommandLineOption webRemoteDebuggingPortOpt("remote-debugging-port",
                                                 TR("WebEngine remote debugging port."),
                                                 "port_number");
    webRemoteDebuggingPortOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(webRemoteDebuggingPortOpt);

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

    return ParseResult::Ok;
}
