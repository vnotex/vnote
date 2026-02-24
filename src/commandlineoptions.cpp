#include "commandlineoptions.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>

// MIGRATION: Use QCoreApplication::translate instead of MainWindow::tr
// #include <widgets/mainwindow.h>
// using vnotex::MainWindow;

// Translation context for command line options
static const char *c_context = "vnotex::MainWindow";

CommandLineOptions::ParseResult CommandLineOptions::parse(const QStringList &p_arguments) {
  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(c_context, "A pleasant note-taking platform."));
  const auto helpOpt = parser.addHelpOption();
  const auto versionOpt = parser.addVersionOption();

  // Positional arguments.
  parser.addPositionalArgument("paths", QCoreApplication::translate(c_context, "Files or folders to open."));

  const QCommandLineOption verboseOpt("verbose", QCoreApplication::translate(c_context, "Print more logs."));
  parser.addOption(verboseOpt);

  const QCommandLineOption logStderrOpt("log-stderr", QCoreApplication::translate(c_context, "Log to stderr."));
  parser.addOption(logStderrOpt);

  const QCommandLineOption watchThemesOpt("watch-themes",
                                          QCoreApplication::translate(c_context, "Watch theme folder for changes."));
  parser.addOption(watchThemesOpt);

  // WebEngine options.
  // No need to handle them. Just add them to the parser to avoid parse error.
  {
    QCommandLineOption webRemoteDebuggingPortOpt("remote-debugging-port",
                                                 QCoreApplication::translate(c_context, "WebEngine remote debugging port."),
                                                 QCoreApplication::translate(c_context, "port_number"));
    webRemoteDebuggingPortOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(webRemoteDebuggingPortOpt);

    QCommandLineOption webNoSandboxOpt("no-sandbox", QCoreApplication::translate(c_context, "WebEngine without sandbox."));
    webNoSandboxOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(webNoSandboxOpt);

    QCommandLineOption webDisableGpu("disable-gpu", QCoreApplication::translate(c_context, "WebEngine with GPU disabled."));
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

  if (parser.isSet(watchThemesOpt)) {
    m_watchThemes = true;
  }

  return ParseResult::Ok;
}
