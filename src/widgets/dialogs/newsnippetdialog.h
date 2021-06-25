#ifndef NEWSNIPPETDIALOG_H
#define NEWSNIPPETDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class SnippetInfoWidget;

    class NewSnippetDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        explicit NewSnippetDialog(QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupSnippetInfoWidget(QWidget *p_parent);

        bool newSnippet();

        bool validateNameInput(QString &p_msg);

        bool validateInputs();

        SnippetInfoWidget *m_infoWidget = nullptr;
    };
}

#endif // NEWSNIPPETDIALOG_H
