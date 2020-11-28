#ifndef IMPORTLEGACYNOTEBOOKDIALOG_H
#define IMPORTLEGACYNOTEBOOKDIALOG_H

#include "newnotebookdialog.h"

namespace vnotex
{
    class ImportLegacyNotebookDialog : public NewNotebookDialog
    {
        Q_OBJECT
    public:
        explicit ImportLegacyNotebookDialog(QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

        bool validateRootFolderInput(QString &p_msg) Q_DECL_OVERRIDE;

    private:
        bool importLegacyNotebook();
    };
}

#endif // IMPORTLEGACYNOTEBOOKDIALOG_H
