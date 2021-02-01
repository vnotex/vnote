#include "nodeinfowidget.h"

#include <QtWidgets>

#include "notebook/notebook.h"
#include "../widgetsfactory.h"
#include <utils/pathutils.h>
#include <utils/utils.h>
#include "exception.h"
#include "nodelabelwithupbutton.h"
#include <utils/widgetutils.h>
#include <buffer/filetypehelper.h>

using namespace vnotex;

NodeInfoWidget::NodeInfoWidget(const Node *p_node, QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Edit)
{
    setupUI(p_node->getParent(), p_node->getFlags());

    setNode(p_node);
}

NodeInfoWidget::NodeInfoWidget(const Node *p_parentNode,
                               Node::Flags p_flags,
                               QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(Mode::Create)
{
    setupUI(p_parentNode, p_flags);
}

void NodeInfoWidget::setupUI(const Node *p_parentNode, Node::Flags p_newNodeFlags)
{
    const bool createMode = m_mode == Mode::Create;
    const bool isNote = p_newNodeFlags & Node::Flag::Content;

    m_mainLayout = WidgetUtils::createFormLayout(this);

    m_mainLayout->addRow(tr("Notebook:"),
                         new QLabel(p_parentNode->getNotebook()->getName(), this));

    m_parentNodeLabel = new NodeLabelWithUpButton(p_parentNode, this);
    m_parentNodeLabel->setReadOnly(!createMode);
    connect(m_parentNodeLabel, &NodeLabelWithUpButton::nodeChanged,
            this, &NodeInfoWidget::inputEdited);
    m_mainLayout->addRow(tr("Location:"), m_parentNodeLabel);

    if (createMode && isNote) {
        setupFileTypeComboBox(this);
        m_mainLayout->addRow(tr("File type:"), m_fileTypeComboBox);
    }

    setupNameLineEdit(this);
    m_mainLayout->addRow(tr("Name:"), m_nameLineEdit);

    if (!createMode) {
        m_createdDateTimeLabel = new QLabel(this);
        m_mainLayout->addRow(tr("Created time:"), m_createdDateTimeLabel);
    }

    if (!createMode && isNote) {
        m_modifiedDateTimeLabel = new QLabel(this);
        m_mainLayout->addRow(tr("Modified time:"), m_modifiedDateTimeLabel);
    }
}

void NodeInfoWidget::setupNameLineEdit(QWidget *p_parent)
{
    m_nameLineEdit = WidgetsFactory::createLineEdit(p_parent);
    auto validator = new QRegularExpressionValidator(QRegularExpression(PathUtils::c_fileNameRegularExpression),
                                                     m_nameLineEdit);
    m_nameLineEdit->setValidator(validator);
    connect(m_nameLineEdit, &QLineEdit::textEdited,
            this, [this]() {
                // Choose the correct file type.
                if (m_fileTypeComboBox) {
                    auto inputName = m_nameLineEdit->text();
                    QString typeName;
                    int dotIdx = inputName.lastIndexOf(QLatin1Char('.'));
                    if (dotIdx != -1) {
                        auto suffix = inputName.mid(dotIdx + 1);
                        const auto &fileType = FileTypeHelper::getInst().getFileTypeBySuffix(suffix);
                        typeName = fileType.m_typeName;
                    } else {
                        typeName = FileTypeHelper::getInst().getFileType(FileTypeHelper::Others).m_typeName;
                    }

                    int idx = m_fileTypeComboBox->findData(typeName);
                    if (idx != -1) {
                        m_fileTypeComboBoxMuted = true;
                        m_fileTypeComboBox->setCurrentIndex(idx);
                        m_fileTypeComboBoxMuted = false;
                    }
                }

                emit inputEdited();
            });
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

    Q_ASSERT(m_mode != Mode::Create);
    m_node = p_node;
    if (m_node) {
        Q_ASSERT(getNotebook() == m_node->getNotebook());
        m_nameLineEdit->setText(m_node->getName());
        m_parentNodeLabel->setNode(m_node->getParent());

        auto createdTime = Utils::dateTimeString(m_node->getCreatedTimeUtc().toLocalTime());
        m_createdDateTimeLabel->setText(createdTime);

        if (m_modifiedDateTimeLabel) {
            auto modifiedTime = Utils::dateTimeString(m_node->getModifiedTimeUtc().toLocalTime());
            m_modifiedDateTimeLabel->setText(modifiedTime);
        }
    }
}

void NodeInfoWidget::setupFileTypeComboBox(QWidget *p_parent)
{
    m_fileTypeComboBox = WidgetsFactory::createComboBox(p_parent);
    const auto &fileTypes = FileTypeHelper::getInst().getAllFileTypes();
    for (const auto &ft : fileTypes) {
        m_fileTypeComboBox->addItem(ft.m_displayName, ft.m_typeName);
    }
    connect(m_fileTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                if (m_fileTypeComboBoxMuted) {
                    return;
                }
                auto name = m_fileTypeComboBox->currentData().toString();
                const auto &fileType = FileTypeHelper::getInst().getFileTypeByName(name);
                const auto suffix = fileType.preferredSuffix();
                if (!suffix.isEmpty()) {
                    // Change the suffix.
                    auto inputName = m_nameLineEdit->text();
                    QString newName;
                    int dotIdx = inputName.lastIndexOf(QLatin1Char('.'));
                    if (dotIdx == -1) {
                        newName = inputName + QLatin1Char('.') + suffix;
                    } else if (inputName.mid(dotIdx + 1) != suffix) {
                        newName = inputName.left(dotIdx + 1) + suffix;
                    }

                    if (!newName.isEmpty()) {
                        m_nameLineEdit->setText(newName);
                        emit inputEdited();
                    }
                }

                WidgetUtils::selectBaseName(m_nameLineEdit);
                m_nameLineEdit->setFocus();
            });
}
