#ifndef IMPORTNOTEBOOKDIALOG_H
#define IMPORTNOTEBOOKDIALOG_H

#include "scrolldialog.h"

class QGroupBox;
class QComboBox;

namespace vnotex
{
    class NotebookInfoWidget;
    class Notebook;

    class ImportNotebookDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        explicit ImportNotebookDialog(QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void validateInputs();

    private:
        void setupUI();

        void setupNotebookInfoWidget(QWidget *p_parent = nullptr);

        bool validateRootFolderInput(QString &p_msg);

        bool createNotebookToImport(QString &p_msg);

        bool importNotebook();

        NotebookInfoWidget *m_infoWidget = nullptr;

        QSharedPointer<Notebook> m_notebookToImport;
    };
} // ns vnotex

#endif // IMPORTNOTEBOOKDIALOG_H
