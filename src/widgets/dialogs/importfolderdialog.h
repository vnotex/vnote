#ifndef IMPORTFOLDERDIALOG_H
#define IMPORTFOLDERDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class Node;
    class FolderFilesFilterWidget;

    class ImportFolderDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        // Import a folder under @p_node.
        ImportFolderDialog(Node *p_node, QWidget *p_parent = nullptr);

        const QSharedPointer<Node> &getNewNode() const;

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void validateInputs();

    private:
        void setupUI();

        void setupFolderFilesFilterWidget(QWidget *p_parent = nullptr);

        bool importFolder();

        Node *m_parentNode = nullptr;

        QSharedPointer<Node> m_newNode;

        FolderFilesFilterWidget *m_filterWidget = nullptr;
    };
}

#endif // IMPORTFOLDERDIALOG_H
