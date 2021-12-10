#ifndef TERMINALVIEWER_H
#define TERMINALVIEWER_H

#include <QFrame>

class QPlainTextEdit;

namespace vnotex
{
    class TitleBar;

    class TerminalViewer : public QFrame
    {
        Q_OBJECT
    public:
        explicit TerminalViewer(QWidget *p_parent = nullptr);

        void append(const QString &p_text);

        void clear();

    private:
        void setupUI();

        void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

        TitleBar *m_titleBar = nullptr;

        QPlainTextEdit *m_consoleEdit = nullptr;
    };
}

#endif // TERMINALVIEWER_H
