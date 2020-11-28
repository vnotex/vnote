#ifndef NEWNOTEBOOKFROMFOLDERDIALOG_H
#define NEWNOTEBOOKFROMFOLDERDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class NotebookInfoWidget;
    class FolderFilesFilterWidget;
    class Node;
    class Notebook;

    class NewNotebookFromFolderDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        explicit NewNotebookFromFolderDialog(QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void validateInputs();

    private:
        void setupUI();

        void setupFolderFilesFilterWidget(QWidget *p_parent = nullptr);

        void setupNotebookInfoWidget(QWidget *p_parent = nullptr);

        bool validateNameInput(QString &p_msg);

        bool validateRootFolderInput(QString &p_msg);

        // Create a new notebook.
        // Return true if succeeded.
        bool newNotebookFromFolder();

        NotebookInfoWidget *m_infoWidget = nullptr;

        FolderFilesFilterWidget *m_filterWidget = nullptr;
    };
}

#endif // NEWNOTEBOOKFROMFOLDERDIALOG_H
