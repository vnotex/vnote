#include "nodeinfowidget.h"

#include <QtWidgets>

#include "notebook/notebook.h"
#include "../widgetsfactory.h"
#include <utils/pathutils.h>
#include <utils/utils.h>
#include "exception.h"
#include "nodelabelwithupbutton.h"

using namespace vnotex;

NodeInfoWidget::NodeInfoWidget(const Node *p_node, QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Edit)
{
    setupUI(p_node->getParent());

    setNode(p_node);

    setStateAccordingToModeAndNodeType(p_node->getType());
}

NodeInfoWidget::NodeInfoWidget(const Node *p_parentNode,
                               Node::Type p_typeToCreate,
                               QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Create)
{
    setupUI(p_parentNode);

    setStateAccordingToModeAndNodeType(p_typeToCreate);
}

void NodeInfoWidget::setupUI(const Node *p_parentNode)
{
    m_mainLayout = new QFormLayout(this);

    m_mainLayout->addRow(tr("Notebook:"),
                         new QLabel(p_parentNode->getNotebook()->getName(), this));

    m_parentNodeLabel = new NodeLabelWithUpButton(p_parentNode, this);
    connect(m_parentNodeLabel, &NodeLabelWithUpButton::nodeChanged,
            this, &NodeInfoWidget::inputEdited);
    m_mainLayout->addRow(tr("Location:"), m_parentNodeLabel);

    setupNameLineEdit(this);
    m_mainLayout->addRow(tr("Name:"), m_nameLineEdit);

    m_createdDateTimeLabel = new QLabel(this);
    m_mainLayout->addRow(tr("Created time:"), m_createdDateTimeLabel);

    m_modifiedDateTimeLabel = new QLabel(this);
    m_mainLayout->addRow(tr("Modified time:"), m_modifiedDateTimeLabel);
}

void NodeInfoWidget::setupNameLineEdit(QWidget *p_parent)
{
    m_nameLineEdit = WidgetsFactory::createLineEdit(p_parent);
    auto validator = new QRegularExpressionValidator(QRegularExpression(PathUtils::c_fileNameRegularExpression),
                                                     m_nameLineEdit);
    m_nameLineEdit->setValidator(validator);
    connect(m_nameLineEdit, &QLineEdit::textEdited,
            this, &NodeInfoWidget::inputEdited);
}

void NodeInfoWidget::setStateAccordingToModeAndNodeType(Node::Type p_type)
{
    bool createMode = m_mode == Mode::Create;
    bool isNote = p_type == Node::Type::File;

    m_parentNodeLabel->setReadOnly(!createMode);

    bool visible = !createMode;
    m_createdDateTimeLabel->setVisible(visible);
    m_mainLayout->labelForField(m_createdDateTimeLabel)->setVisible(visible);

    visible = !createMode && isNote;
    m_modifiedDateTimeLabel->setVisible(visible);
    m_mainLayout->labelForField(m_modifiedDateTimeLabel)->setVisible(visible);
}

QLineEdit *NodeInfoWidget::getNameLineEdit() const
{
    return m_nameLineEdit;
}

QString NodeInfoWidget::getName() const
{
    return getNameLineEdit()->text().trimmed();
}

const Notebook *NodeInfoWidget::getNotebook() const
{
    return getParentNode()->getNotebook();
}

const Node *NodeInfoWidget::getParentNode() const
{
    return m_parentNodeLabel->getNode();
}

void NodeInfoWidget::setNode(const Node *p_node)
{
    if (m_node == p_node) {
        return;
    }

    m_node = p_node;
    if (m_node) {
        Q_ASSERT(getNotebook() == m_node->getNotebook());
        m_nameLineEdit->setText(m_node->getName());
        m_parentNodeLabel->setNode(m_node->getParent());

        auto createdTime = Utils::dateTimeString(m_node->getCreatedTimeUtc().toLocalTime());
        m_createdDateTimeLabel->setText(createdTime);
        auto modifiedTime = Utils::dateTimeString(m_node->getModifiedTimeUtc().toLocalTime());
        m_modifiedDateTimeLabel->setText(modifiedTime);
    }
}
