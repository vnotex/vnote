#include "newnotedialog.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <QPlainTextEdit>

#include "notebook/notebook.h"
#include "notebook/node.h"
#include "../widgetsfactory.h"
#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include "exception.h"
#include "nodeinfowidget.h"
#include "notetemplateselector.h"
#include <utils/widgetutils.h>
#include <core/templatemgr.h>
#include <core/configmgr.h>
#include <core/widgetconfig.h>
#include <snippet/snippetmgr.h>
#include <buffer/filetypehelper.h>

using namespace vnotex;

QString NewNoteDialog::s_lastTemplate;

NewNoteDialog::NewNoteDialog(Node *p_node, QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    Q_ASSERT(p_node && p_node->isLoaded());
    setupUI(p_node);

    initDefaultValues(p_node);

    m_infoWidget->getNameLineEdit()->setFocus();
}

void NewNoteDialog::setupUI(const Node *p_node)
{
    setupNodeInfoWidget(p_node, this);
    setCentralWidget(m_infoWidget);

    auto infoLayout = m_infoWidget->getMainLayout();

    m_templateSelector = new NoteTemplateSelector(m_infoWidget);
    infoLayout->addRow(tr("Template:"), m_templateSelector);

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(tr("New Note"));
}

void NewNoteDialog::setupNodeInfoWidget(const Node *p_node, QWidget *p_parent)
{
    m_infoWidget = new NodeInfoWidget(p_node, Node::Flag::Content, p_parent);
}

bool NewNoteDialog::validateInputs()
{
    bool valid = true;
    QString msg;

    valid = valid && validateNameInput(msg);
    setInformationText(msg, valid ? ScrollDialog::InformationLevel::Info
                                  : ScrollDialog::InformationLevel::Error);
    return valid;
}

bool NewNoteDialog::validateNameInput(QString &p_msg)
{
    p_msg.clear();

    auto name = m_infoWidget->getName();
    if (name.isEmpty() || !PathUtils::isLegalFileName(name)) {
        p_msg = tr("Please specify a valid name for the note.");
        return false;
    }

    if (!m_infoWidget->getParentNode()->isLegalNameForNewChild(name)) {
        p_msg = tr("Name conflicts with existing or built-in note.");
        return false;
    }

    return true;
}

void NewNoteDialog::acceptedButtonClicked()
{
    s_lastTemplate = m_templateSelector->getCurrentTemplate();

    {
        auto fileType = FileTypeHelper::getInst().getFileTypeByName(m_infoWidget->getFileType()).m_type;
        ConfigMgr::getInst().getWidgetConfig().setNewNoteDefaultFileType(static_cast<int>(fileType));
    }

    if (validateInputs()) {
        Notebook *notebook = const_cast<Notebook *>(m_infoWidget->getNotebook());
        Node *parentNode = const_cast<Node *>(m_infoWidget->getParentNode());
        QString errMsg;
        m_newNode = newNote(notebook,
                            parentNode,
                            m_infoWidget->getName(),
                            m_templateSelector->getTemplateContent(),
                            errMsg);
        if (!m_newNode) {
            setInformationText(errMsg, ScrollDialog::InformationLevel::Error);
            return;
        }
        accept();
    }
}

QSharedPointer<Node> NewNoteDialog::newNote(Notebook *p_notebook,
                                            Node *p_parentNode,
                                            const QString &p_name,
                                            const QString &p_templateContent,
                                            QString &p_errMsg)
{
    Q_ASSERT(p_notebook && p_parentNode);

    QSharedPointer<Node> newNode;
    p_errMsg.clear();

    try {
        newNode = p_notebook->newNode(p_parentNode,
                                      Node::Flag::Content,
                                      p_name,
                                      evaluateTemplateContent(p_templateContent, p_name));
    } catch (Exception &p_e) {
        p_errMsg = tr("Failed to create note under (%1) in (%2) (%3).").arg(p_parentNode->getName(),
                                                                            p_notebook->getName(),
                                                                            p_e.what());
        qCritical() << p_errMsg;
        return nullptr;
    }

    emit p_notebook->nodeUpdated(newNode.data());
    return newNode;
}

const QSharedPointer<Node> &NewNoteDialog::getNewNode() const
{
    return m_newNode;
}

void NewNoteDialog::initDefaultValues(const Node *p_node)
{
    {
        int defaultType = ConfigMgr::getInst().getWidgetConfig().getNewNoteDefaultFileType();
        const auto &fileType = FileTypeHelper::getInst().getFileType(defaultType);

        m_infoWidget->setFileType(fileType.m_typeName);

        auto lineEdit = m_infoWidget->getNameLineEdit();
        auto defaultName = FileUtils::generateFileNameWithSequence(p_node->fetchAbsolutePath(),
                                                                   tr("note"),
                                                                   fileType.preferredSuffix());
        lineEdit->setText(defaultName);
        WidgetUtils::selectBaseName(lineEdit);
    }

    if (!s_lastTemplate.isEmpty()) {
        // Restore.
        if (!m_templateSelector->setCurrentTemplate(s_lastTemplate)) {
            s_lastTemplate.clear();
        }
    }
}

QString NewNoteDialog::evaluateTemplateContent(const QString &p_content, const QString &p_name)
{
    int cursorOffset = 0;
    return SnippetMgr::getInst().applySnippetBySymbol(p_content,
                                                      QString(),
                                                      cursorOffset,
                                                      SnippetMgr::generateOverrides(p_name));
}
