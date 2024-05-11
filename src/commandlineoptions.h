#ifndef COMMANDLINEOPTIONS_H
#define COMMANDLINEOPTIONS_H

#include <QStringList>

class CommandLineOptions
{
public:
    enum ParseResult
    {
        Ok,
        Error,
        VersionRequested,
        HelpRequested
    };

    CommandLineOptions() = default;

    ParseResult parse(const QStringList &p_arguments);

    QString m_errorMsg;

    QString m_helpText;

    QStringList m_pathsToOpen;

    bool m_verbose = false;

    bool m_logToStderr = false;
};

#endif // COMMANDLINEOPTIONS_H
