#ifndef NODELABELWITHUPBUTTON_H
#define NODELABELWITHUPBUTTON_H

#include <QWidget>

class QLabel;
class QPushButton;

namespace vnotex
{
    class Node;

    class NodeLabelWithUpButton : public QWidget
    {
        Q_OBJECT
    public:
        NodeLabelWithUpButton(const Node *p_node, QWidget *p_parent = nullptr);

        const Node *getNode() const;

        void setNode(const Node *p_node);

        void setReadOnly(bool p_readonly);

    signals:
        void nodeChanged(const Node *p_node);

    private:
        void setupUI();

        void updateLabelAndButton();

        QLabel *m_label = nullptr;

        QPushButton *m_upButton = nullptr;

        const Node *m_node = nullptr;

        bool m_readOnly = false;
    };
} // ns vnotex

#endif // NODELABELWITHUPBUTTON_H
