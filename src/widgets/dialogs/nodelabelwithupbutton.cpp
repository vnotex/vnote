#include "nodelabelwithupbutton.h"

#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>

#include "notebook/node.h"
#include <utils/iconutils.h>
#include "vnotex.h"

using namespace vnotex;

NodeLabelWithUpButton::NodeLabelWithUpButton(const Node *p_node, QWidget *p_parent)
    : QWidget(p_parent),
      m_node(p_node)
{
    setupUI();
}

void NodeLabelWithUpButton::setupUI()
{
    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_label = new QLabel(this);
    mainLayout->addWidget(m_label, 1);

    auto iconFile = VNoteX::getInst().getThemeMgr().getIconFile("up_parent_node.svg");
    m_upButton = new QPushButton(IconUtils::fetchIconWithDisabledState(iconFile),
                                 tr("Up"),
                                 this);
    m_upButton->setToolTip(tr("Create note under an upper level node"));
    connect(m_upButton, &QPushButton::clicked,
            this, [this]() {
                if (!m_node->isRoot()) {
                    m_node = m_node->getParent();
                    updateLabelAndButton();
                    emit nodeChanged(m_node);
                }
            });
    mainLayout->addWidget(m_upButton, 0);

    updateLabelAndButton();
}

void NodeLabelWithUpButton::updateLabelAndButton()
{
    m_label->setText(m_node->fetchPath());
    m_upButton->setVisible(!m_readOnly && !m_node->isRoot());
}

const Node *NodeLabelWithUpButton::getNode() const
{
    return m_node;
}

void NodeLabelWithUpButton::setNode(const Node *p_node)
{
    if (m_node == p_node) {
        return;
    }

    m_node = p_node;
    updateLabelAndButton();
    emit nodeChanged(m_node);
}

void NodeLabelWithUpButton::setReadOnly(bool p_readonly)
{
    if (m_readOnly == p_readonly) {
        return;
    }

    m_readOnly = p_readonly;
    updateLabelAndButton();
}
