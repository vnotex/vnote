#ifndef FOLDERPROPERTIESDIALOG_H
#define FOLDERPROPERTIESDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class Node;
    class NodeInfoWidget;

    class FolderPropertiesDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        FolderPropertiesDialog(Node *p_node, QWidget *p_parent = nullptr);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void validateInputs();

    private:
        void setupUI();

        void setupNodeInfoWidget(QWidget *p_parent);

        bool validateNameInput(QString &p_msg);

        bool saveFolderProperties();

        NodeInfoWidget *m_infoWidget = nullptr;

        Node *m_node = nullptr;
    };
} // ns vnotex

#endif // FOLDERPROPERTIESDIALOG_H
