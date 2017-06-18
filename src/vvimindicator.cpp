#include "vvimindicator.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QStringList>
#include <QFontMetrics>
#include <QFont>
#include <QHeaderView>

#include "vconfigmanager.h"
#include "vbuttonwithwidget.h"

extern VConfigManager vconfig;

VVimIndicator::VVimIndicator(QWidget *parent)
    : QWidget(parent), m_vim(NULL)
{
    setupUI();
}

void VVimIndicator::setupUI()
{
    m_modeLabel = new QLabel(this);

    m_regBtn = new VButtonWithWidget(QIcon(":/resources/icons/arrow_dropup.svg"),
                                     "\"",
                                     this);
    m_regBtn->setProperty("StatusBtn", true);
    m_regBtn->setFocusPolicy(Qt::NoFocus);
    QTreeWidget *regTree = new QTreeWidget(this);
    regTree->setColumnCount(2);
    regTree->header()->setStretchLastSection(true);
    regTree->header()->setProperty("RegisterTree", true);
    QStringList headers;
    headers << tr("Register") << tr("Value");
    regTree->setHeaderLabels(headers);
    m_regBtn->setPopupWidget(regTree);
    connect(m_regBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VVimIndicator::updateRegistersTree);

    m_keyLabel = new QLabel(this);
    QFontMetrics metric(font());
    m_keyLabel->setMinimumWidth(metric.width('A') * 5);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_modeLabel);
    mainLayout->addWidget(m_regBtn);
    mainLayout->addWidget(m_keyLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

QString VVimIndicator::modeToString(VimMode p_mode) const
{
    QString str;

    switch (p_mode) {
    case VimMode::Normal:
       str = tr("Normal");
       break;

    case VimMode::Insert:
       str = tr("Insert");
       break;

    case VimMode::Visual:
       str = tr("Visual");
       break;

    case VimMode::VisualLine:
       str = tr("VisualLine");
       break;

    case VimMode::Replace:
       str = tr("Replace");
       break;

    default:
       str = tr("Unknown");
        break;
    }

    return str;
}

static QString modeBackgroundColor(VimMode p_mode)
{
    QString color;

    switch (p_mode) {
    case VimMode::Normal:
       color = vconfig.getEditorVimNormalBg();
       break;

    case VimMode::Insert:
       color = vconfig.getEditorVimInsertBg();
       break;

    case VimMode::Visual:
       color = vconfig.getEditorVimVisualBg();
       break;

    case VimMode::VisualLine:
       color = vconfig.getEditorVimVisualBg();
       break;

    case VimMode::Replace:
       color = vconfig.getEditorVimReplaceBg();
       break;

    default:
       color = "red";
       break;
    }

    return color;
}

static void fillTreeItemsWithRegisters(QTreeWidget *p_tree,
                                       const QMap<QChar, VVim::Register> &p_regs)
{
    p_tree->clear();
    for (auto const &reg : p_regs) {
        if (reg.m_value.isEmpty()) {
            continue;
        }

        QStringList itemStr;
        itemStr << reg.m_name << reg.m_value;
        QTreeWidgetItem *item = new QTreeWidgetItem(p_tree, itemStr);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    }

    p_tree->resizeColumnToContents(0);
    p_tree->resizeColumnToContents(1);
}

void VVimIndicator::update(const VVim *p_vim)
{
    if (!p_vim) {
        m_vim = p_vim;
        return;
    }

    m_vim = p_vim;

    VimMode mode = p_vim->getMode();
    QChar curRegName = p_vim->getCurrentRegisterName();

    QString style = QString("QLabel { padding: 0px 2px 0px 2px; font: bold; background-color: %1; }")
                           .arg(modeBackgroundColor(mode));
    m_modeLabel->setStyleSheet(style);
    m_modeLabel->setText(modeToString(mode));

    m_regBtn->setText(curRegName);

    QString keyText = QString("<span style=\"font-weight:bold;\">%1</span>")
                             .arg(p_vim->getPendingKeys());
    m_keyLabel->setText(keyText);
}

void VVimIndicator::updateRegistersTree(QWidget *p_widget)
{
    QTreeWidget *regTree = dynamic_cast<QTreeWidget *>(p_widget);
    if (!m_vim) {
        regTree->clear();
        return;
    }

    const QMap<QChar, VVim::Register> &regs = m_vim->getRegisters();
    fillTreeItemsWithRegisters(regTree, regs);
}
