#ifndef MANAGENOTEBOOKSDIALOG_H
#define MANAGENOTEBOOKSDIALOG_H

#include "dialog.h"

class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QScrollArea;

namespace vnotex
{
    class Notebook;
    class NotebookInfoWidget;

    class ManageNotebooksDialog : public Dialog
    {
        Q_OBJECT
    public:
        ManageNotebooksDialog(const Notebook *p_notebook, QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

        void resetButtonClicked() Q_DECL_OVERRIDE;

        void appliedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void selectNotebook(Notebook *p_notebook);

    private:
        void setupUI();

        void setupNotebookList(QWidget *p_parent = nullptr);

        void setupNotebookInfoWidget(QWidget *p_parent = nullptr);

        void loadNotebooks(const Notebook *p_notebook);

        void setChangesUnsaved(bool p_unsaved);

        bool saveChangesToNotebook();

        Notebook *getNotebookFromItem(const QListWidgetItem *p_item) const;

        void closeNotebook(const Notebook *p_notebook);

        void removeNotebook(const Notebook *p_notebook);

        bool checkUnsavedChanges();

        QListWidget *m_notebookList = nullptr;

        NotebookInfoWidget *m_notebookInfoWidget = nullptr;

        bool m_changesUnsaved = false;

        Notebook *m_notebook = nullptr;

        QScrollArea *m_infoScrollArea = nullptr;

        QPushButton *m_closeNotebookBtn = nullptr;

        QPushButton *m_deleteNotebookBtn = nullptr;
    };
} // ns vnotex

#endif // MANAGENOTEBOOKSDIALOG_H
