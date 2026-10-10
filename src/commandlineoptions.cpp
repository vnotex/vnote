#include "commandlineoptions.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>

CommandLineOptions::ParseResult CommandLineOptions::parse(const QStringList &p_arguments) {
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("CommandLineOptions", "A pleasant note-taking platform."));
  const auto helpOpt = parser.addHelpOption();
  const auto versionOpt = parser.addVersionOption();

  // Positional arguments.
  parser.addPositionalArgument(
      "paths", QCoreApplication::translate("CommandLineOptions", "Files or folders to open."));

  const QCommandLineOption verboseOpt(
      "verbose", QCoreApplication::translate("CommandLineOptions", "Print more logs."));
  parser.addOption(verboseOpt);

  const QCommandLineOption logStderrOpt(
      "log-stderr", QCoreApplication::translate("CommandLineOptions", "Log to stderr."));
  parser.addOption(logStderrOpt);

  const QCommandLineOption quietOpt(
      "quiet", QCoreApplication::translate("CommandLineOptions", "Suppress non-critical console logs."));
  parser.addOption(quietOpt);

  const QCommandLineOption watchThemesOpt(
      "watch-themes",
      QCoreApplication::translate("CommandLineOptions", "Watch theme folder for changes."));
  parser.addOption(watchThemesOpt);

  const QCommandLineOption detachedViewOpt(
      "detached-view",
      QCoreApplication::translate("CommandLineOptions", "Open files in a detached view split."));
  parser.addOption(detachedViewOpt);

  // WebEngine options.
  // No need to handle them. Just add them to the parser to avoid parse error.
  {
    QCommandLineOption webRemoteDebuggingPortOpt(
        "remote-debugging-port",
        QCoreApplication::translate("CommandLineOptions", "WebEngine remote debugging port."),
        QCoreApplication::translate("CommandLineOptions", "port_number"));
    webRemoteDebuggingPortOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(webRemoteDebuggingPortOpt);

    QCommandLineOption webNoSandboxOpt(
        "no-sandbox", QCoreApplication::translate("CommandLineOptions", "WebEngine without sandbox."));
    webNoSandboxOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(webNoSandboxOpt);

    QCommandLineOption webDisableGpu(
        "disable-gpu", QCoreApplication::translate("CommandLineOptions", "WebEngine with GPU disabled."));
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
  qInfo() << "Command-line parser received:" << p_arguments << "positional paths:" << m_pathsToOpen;

  if (parser.isSet(verboseOpt)) {
    m_verbose = true;
  }

  if (parser.isSet(logStderrOpt)) {
    m_logToStderr = true;
  }

  if (parser.isSet(quietOpt)) {
    m_quiet = true;
  }

  if (parser.isSet(watchThemesOpt)) {
    m_watchThemes = true;
  }

  if (parser.isSet(detachedViewOpt)) {
    m_detachedView = true;
  }

  return ParseResult::Ok;
}
