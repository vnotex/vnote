#ifndef NEWNOTEDIALOG_H
#define NEWNOTEDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class Notebook;
    class Node;
    class NodeInfoWidget;

    class NewNoteDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        // New a note under @p_node.
        NewNoteDialog(Node *p_node, QWidget *p_parent = nullptr);

        const QSharedPointer<Node> &getNewNode() const;

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private slots:
        void validateInputs();

    private:
        void setupUI(const Node *p_node);

        void setupNodeInfoWidget(const Node *p_node, QWidget *p_parent);

        bool validateNameInput(QString &p_msg);

        bool newNote();

        void initDefaultValues(const Node *p_node);

        NodeInfoWidget *m_infoWidget = nullptr;

        QSharedPointer<Node> m_newNode;
    };
} // ns vnotex

#endif // NEWNOTEDIALOG_H
