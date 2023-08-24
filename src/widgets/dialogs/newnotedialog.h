#ifndef NEWNOTEDIALOG_H
#define NEWNOTEDIALOG_H

#include "scrolldialog.h"

namespace vnotex
{
    class Notebook;
    class Node;
    class NodeInfoWidget;
    class NoteTemplateSelector;

    class NewNoteDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        // New a note under @p_node.
        NewNoteDialog(Node *p_node, QWidget *p_parent = nullptr);

        const QSharedPointer<Node> &getNewNode() const;

        static QSharedPointer<Node> newNote(Notebook *p_notebook,
                                            Node *p_parentNode,
                                            const QString &p_name,
                                            const QString &p_templateContent,
                                            QString &p_errMsg);

    protected:
        void acceptedButtonClicked() Q_DECL_OVERRIDE;

    private:
        void setupUI(const Node *p_node);

        void setupNodeInfoWidget(const Node *p_node, QWidget *p_parent);

        bool validateInputs();

        bool validateNameInput(QString &p_msg);

        void initDefaultValues(const Node *p_node);

        static QString evaluateTemplateContent(const QString &p_content, const QString &p_name);

        NodeInfoWidget *m_infoWidget = nullptr;

        NoteTemplateSelector *m_templateSelector = nullptr;

        QSharedPointer<Node> m_newNode;

        static QString s_lastTemplate;
    };
} // ns vnotex

#endif // NEWNOTEDIALOG_H
