#include "vtabindicator.h"

#include <QtWidgets>

#include "vedittab.h"
#include "vorphanfile.h"
#include "vbuttonwithwidget.h"
#include "vwordcountinfo.h"

class VWordCountPanel : public QWidget
{
public:
    VWordCountPanel(QWidget *p_parent)
        : QWidget(p_parent)
    {
        m_wordLabel = new QLabel();
        m_wordLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_charWithoutSpacesLabel = new QLabel();
        m_charWithoutSpacesLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_charWithSpacesLabel = new QLabel();
        m_charWithSpacesLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QFormLayout *readLayout = new QFormLayout();
        readLayout->addRow(tr("Words"), m_wordLabel);
        readLayout->addRow(tr("Characters (no spaces)"), m_charWithoutSpacesLabel);
        readLayout->addRow(tr("Characters (with spaces)"), m_charWithSpacesLabel);
        m_readBox = new QGroupBox(tr("Read"));
        m_readBox->setLayout(readLayout);

        m_wordEditLabel = new QLabel();
        m_wordEditLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_charWithoutSpacesEditLabel = new QLabel();
        m_charWithoutSpacesEditLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_charWithSpacesEditLabel = new QLabel();
        m_charWithSpacesEditLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QLabel *cwsLabel = new QLabel(tr("Characters (with spaces)"));
        cwsLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

        QFormLayout *editLayout = new QFormLayout();
        editLayout->addRow(tr("Words"), m_wordEditLabel);
        editLayout->addRow(tr("Characters (no spaces)"), m_charWithoutSpacesEditLabel);
        editLayout->addRow(cwsLabel, m_charWithSpacesEditLabel);
        m_editBox = new QGroupBox(tr("Edit"));
        m_editBox->setLayout(editLayout);

        QLabel *titleLabel = new QLabel(tr("Word Count"));
        titleLabel->setProperty("TitleLabel", true);
        QVBoxLayout *mainLayout = new QVBoxLayout();
        mainLayout->addWidget(titleLabel);
        mainLayout->addWidget(m_readBox);
        mainLayout->addWidget(m_editBox);

        setLayout(mainLayout);

        setMinimumWidth(300);
    }

    void updateReadInfo(const VWordCountInfo &p_readInfo)
    {
        if (p_readInfo.isNull()) {
            m_wordLabel->clear();
            m_charWithoutSpacesLabel->clear();
            m_charWithSpacesLabel->clear();
        } else {
            m_wordLabel->setText(QString::number(p_readInfo.m_wordCount));
            m_charWithoutSpacesLabel->setText(QString::number(p_readInfo.m_charWithoutSpacesCount));
            m_charWithSpacesLabel->setText(QString::number(p_readInfo.m_charWithSpacesCount));
        }
    }

    void updateEditInfo(const VWordCountInfo &p_editInfo)
    {
        if (p_editInfo.isNull()) {
            m_wordEditLabel->clear();
            m_charWithoutSpacesEditLabel->clear();
            m_charWithSpacesEditLabel->clear();
        } else {
            m_wordEditLabel->setText(QString::number(p_editInfo.m_wordCount));
            m_charWithoutSpacesEditLabel->setText(QString::number(p_editInfo.m_charWithoutSpacesCount));
            m_charWithSpacesEditLabel->setText(QString::number(p_editInfo.m_charWithSpacesCount));
        }
    }

    void clear()
    {
        m_wordLabel->clear();
        m_charWithoutSpacesLabel->clear();
        m_charWithSpacesLabel->clear();

        m_wordEditLabel->clear();
        m_charWithoutSpacesEditLabel->clear();
        m_charWithSpacesEditLabel->clear();
    }

private:
    QLabel *m_wordLabel;
    QLabel *m_charWithoutSpacesLabel;
    QLabel *m_charWithSpacesLabel;

    QLabel *m_wordEditLabel;
    QLabel *m_charWithoutSpacesEditLabel;
    QLabel *m_charWithSpacesEditLabel;

    QGroupBox *m_readBox;
    QGroupBox *m_editBox;
};

VTabIndicator::VTabIndicator(QWidget *p_parent)
    : QWidget(p_parent),
      m_editTab(NULL)
{
    setupUI();
}

void VTabIndicator::setupUI()
{
    m_docTypeLabel = new QLabel(this);
    m_docTypeLabel->setToolTip(tr("The type of the file"));
    m_docTypeLabel->setProperty("ColorGreyLabel", true);

    m_readonlyLabel = new QLabel(tr("ReadOnly"), this);
    m_readonlyLabel->setToolTip(tr("This file is read-only"));
    m_readonlyLabel->setProperty("ColorRedLabel", true);

    m_externalLabel = new QLabel(tr("Standalone"), this);
    m_externalLabel->setToolTip(tr("This file is not managed by any notebook or folder"));
    m_externalLabel->setProperty("ColorTealLabel", true);

    m_systemLabel = new QLabel(tr("System"), this);
    m_systemLabel->setToolTip(tr("This file is a system file"));
    m_systemLabel->setProperty("ColorGreenLabel", true);

    m_cursorLabel = new QLabel(this);
    m_cursorLabel->setProperty("TabIndicatorLabel", true);

    m_wordCountPanel = new VWordCountPanel(this);
    m_wordCountBtn = new VButtonWithWidget(tr("[W]"), m_wordCountPanel, this);
    m_wordCountBtn->setToolTip(tr("Word Count Information"));
    m_wordCountBtn->setProperty("StatusBtn", true);
    m_wordCountBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_wordCountBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VTabIndicator::updateWordCountInfo);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_cursorLabel);
    mainLayout->addWidget(m_wordCountBtn);
    mainLayout->addWidget(m_externalLabel);
    mainLayout->addWidget(m_systemLabel);
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
    const VFile *file = NULL;
    DocType docType = DocType::Html;
    bool readonly = false;
    bool external = false;
    bool system = false;
    QString cursorStr;

    m_editTab = p_info.m_editTab;

    if (m_editTab)
    {
        file = m_editTab->getFile();
        docType = file->getDocType();
        readonly = !file->isModifiable();
        external = file->getType() == FileType::Orphan;
        system = external && dynamic_cast<const VOrphanFile *>(file)->isSystemFile();

        if (m_editTab->isEditMode()) {
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

    updateWordCountBtn(p_info);

    if (p_info.m_wordCountInfo.m_mode == VWordCountInfo::Read) {
        m_wordCountPanel->updateReadInfo(p_info.m_wordCountInfo);
    }

    m_docTypeLabel->setText(docTypeToString(docType));
    m_readonlyLabel->setVisible(readonly);
    m_externalLabel->setVisible(external);
    m_systemLabel->setVisible(system);
}

void VTabIndicator::updateWordCountInfo(QWidget *p_widget)
{
    VWordCountPanel *wcp = dynamic_cast<VWordCountPanel *>(p_widget);
    if (!m_editTab) {
        wcp->clear();
        return;
    }

    wcp->updateReadInfo(m_editTab->fetchWordCountInfo(false));
    wcp->updateEditInfo(m_editTab->fetchWordCountInfo(true));
}

void VTabIndicator::updateWordCountBtn(const VEditTabInfo &p_info)
{
    const VEditTab *editTab = p_info.m_editTab;
    if (!editTab) {
        m_wordCountBtn->setText(tr("[W]"));
        return;
    }

    const VWordCountInfo &wci = p_info.m_wordCountInfo;
    bool editMode = editTab->isEditMode();
    int wc = -1;
    bool needUpdate = false;
    switch (wci.m_mode) {
    case VWordCountInfo::Read:
        if (!editMode) {
            wc = wci.m_wordCount;
            needUpdate = true;
        }

        break;

    case VWordCountInfo::Edit:
        if (editMode) {
            wc = wci.m_charWithSpacesCount;
            needUpdate = true;
        }

        break;

    case VWordCountInfo::Invalid:
        needUpdate = true;
        break;

    default:
        break;
    }

    if (needUpdate) {
        QString text = tr("[%1]%2").arg(editMode ? tr("C") : tr("W"))
                                   .arg(wc > -1 ? QString::number(wc) : "");
        m_wordCountBtn->setText(text);
    }
}
