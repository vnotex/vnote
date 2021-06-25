#ifndef SNIPPETPROPERTIESDIALOG_H
#define SNIPPETPROPERTIESDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class Snippet;
    class SnippetInfoWidget;

    class SnippetPropertiesDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        SnippetPropertiesDialog(Snippet *p_snippet, QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupSnippetInfoWidget(QWidget *p_parent);

        bool validateNameInput(QString &p_msg);

        bool saveSnippetProperties();

        bool validateInputs();

        SnippetInfoWidget *m_infoWidget = nullptr;

        Snippet *m_snippet = nullptr;
    };
}

#endif // SNIPPETPROPERTIESDIALOG_H
