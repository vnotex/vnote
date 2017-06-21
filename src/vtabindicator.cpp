#include "vtabindicator.h"

#include <QLabel>
#include <QHBoxLayout>

#include "vedittab.h"

VTabIndicator::VTabIndicator(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();
}

void VTabIndicator::setupUI()
{
    m_docTypeLabel = new QLabel(this);
    m_readonlyLabel = new QLabel(tr("<span style=\"font-weight:bold; color:red;\">ReadOnly</span>"),
                                 this);
    m_cursorLabel = new QLabel(this);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_cursorLabel);
    mainLayout->addWidget(m_readonlyLabel);
    mainLayout->addWidget(m_docTypeLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

static QString docTypeToString(DocType p_type)
{
    QString str;

    switch (p_type) {
    case DocType::Html:
        str = "HTML";
        break;

    case DocType::Markdown:
        str = "Markdown";
        break;

    case DocType::List:
        str = "List";
        break;

    case DocType::Container:
        str = "Container";
        break;

    default:
        str = "Unknown";
        break;
    }

    return str;
}

void VTabIndicator::update(const VEditTabInfo &p_info)
{
    const VEditTab *editTab = NULL;
    const VFile *file = NULL;
    DocType docType = DocType::Html;
    bool readonly = true;
    QString cursorStr;

    if (p_info.m_editTab)
    {
        editTab = p_info.m_editTab;
        file = editTab->getFile();
        docType = file->getDocType();
        readonly = !file->isModifiable();

        if (editTab->isEditMode()) {
            int line = p_info.m_cursorBlockNumber + 1;
            int col = p_info.m_cursorPositionInBlock;
            if (col < 0) {
                col = 0;
            }

            int lineCount = p_info.m_blockCount < 1 ? 1 : p_info.m_blockCount;

            QString cursorText = tr("<span><span style=\"font-weight:bold;\">Line</span>: %1 - %2(%3%)  "
                                    "<span style=\"font-weight:bold;\">Col</span>: %4</span>")
                                   .arg(line).arg(lineCount)
                                   .arg((int)(line * 1.0 / lineCount * 100), 2)
                                   .arg(col, 3);
            m_cursorLabel->setText(cursorText);
            m_cursorLabel->show();
        } else {
            m_cursorLabel->hide();
        }
    }

    m_docTypeLabel->setText(docTypeToString(docType));
    m_readonlyLabel->setVisible(readonly);
}
