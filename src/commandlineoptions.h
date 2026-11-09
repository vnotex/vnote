#ifndef COMMANDLINEOPTIONS_H
#define COMMANDLINEOPTIONS_H

#include <QStringList>

#include <core/global.h>

class CommandLineOptions {
public:
  enum ParseResult { Ok, Error, VersionRequested, HelpRequested };

  CommandLineOptions() = default;

  ParseResult parse(const QStringList &p_arguments);

  QString m_errorMsg;

  QString m_helpText;

  QStringList m_pathsToOpen;

  bool m_verbose = false;

  bool m_logToStderr = false;

  // Whether to suppress non-critical console logs.
  bool m_quiet = false;

  // Whether to watch theme folder for changes
  bool m_watchThemes = false;

  // Whether to open the provided files in a detached view split.
  bool m_detachedView = false;

  // Invalid preserves the normal per-file mode policy.
  vnotex::ViewWindowMode m_mode = vnotex::ViewWindowMode::Invalid;

  // Testing only: skip the instance lock and IPC, without isolating user data.
  bool m_noSingleInstance = false;
};

#endif // COMMANDLINEOPTIONS_H
