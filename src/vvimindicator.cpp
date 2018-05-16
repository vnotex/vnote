#include "vvimindicator.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QStringList>
#include <QFontMetrics>
#include <QFont>
#include <QHeaderView>
#include <QInputMethod>
#include <QGuiApplication>

#include "vconfigmanager.h"
#include "vbuttonwithwidget.h"
#include "vpalette.h"

extern VConfigManager *g_config;

VVimIndicator::VVimIndicator(QWidget *p_parent)
    : QWidget(p_parent), m_vim(NULL)
{
    setupUI();
}

void VVimIndicator::setupUI()
{
    m_modeLabel = new QLabel(this);
    m_modeLabel->setProperty("VimIndicatorModeLabel", true);

    QTreeWidget *regTree = new QTreeWidget(this);
    regTree->setProperty("ItemBorder", true);
    regTree->setRootIsDecorated(false);
    regTree->setColumnCount(2);
    regTree->header()->setStretchLastSection(true);
    QStringList headers;
    headers << tr("Register") << tr("Value");
    regTree->setHeaderLabels(headers);

    m_regBtn = new VButtonWithWidget("\"",
                                     regTree,
                                     this);
    m_regBtn->setToolTip(tr("Registers"));
    m_regBtn->setProperty("StatusBtn", true);
    m_regBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_regBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VVimIndicator::updateRegistersTree);

    QTreeWidget *markTree = new QTreeWidget(this);
    markTree->setProperty("ItemBorder", true);
    markTree->setRootIsDecorated(false);
    markTree->setColumnCount(4);
    markTree->header()->setStretchLastSection(true);
    headers.clear();
    headers << tr("Mark") << tr("Line") << tr("Column") << tr("Text");
    markTree->setHeaderLabels(headers);

    m_markBtn = new VButtonWithWidget("[]",
                                      markTree,
                                      this);
    m_markBtn->setToolTip(tr("Marks"));
    m_markBtn->setProperty("StatusBtn", true);
    m_markBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_markBtn, &VButtonWithWidget::popupWidgetAboutToShow,
            this, &VVimIndicator::updateMarksTree);

    m_keyLabel = new QLabel(this);
    m_keyLabel->setProperty("VimIndicatorKeyLabel", true);
    QFontMetrics metric(font());
    m_keyLabel->setMinimumWidth(metric.width('A') * 5);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_modeLabel);
    mainLayout->addWidget(m_regBtn);
    mainLayout->addWidget(m_markBtn);
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
       color = g_config->getEditorVimNormalBg();
       break;

    case VimMode::Insert:
       color = g_config->getEditorVimInsertBg();
       break;

    case VimMode::Visual:
       color = g_config->getEditorVimVisualBg();
       break;

    case VimMode::VisualLine:
       color = g_config->getEditorVimVisualBg();
       break;

    case VimMode::Replace:
       color = g_config->getEditorVimReplaceBg();
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
    m_vim = const_cast<VVim *>(p_vim);
    if (!m_vim) {
        return;
    }

    VimMode mode = VimMode::Normal;
    QChar curRegName(' ');
    QChar lastUsedMark;
    QString pendingKeys;
    if (p_vim) {
        mode = p_vim->getMode();
        curRegName = p_vim->getCurrentRegisterName();
        lastUsedMark = p_vim->getMarks().getLastUsedMark();
        pendingKeys = p_vim->getPendingKeys();
    }

    m_modeLabel->setStyleSheet(QString("background: %1;").arg(modeBackgroundColor(mode)));
    m_modeLabel->setText(modeToString(mode));

    m_regBtn->setText(curRegName);

    QString markText = QString("[%1]")
                              .arg(lastUsedMark.isNull() ? QChar(' ') : lastUsedMark);
    m_markBtn->setText(markText);

    m_keyLabel->setText(pendingKeys);
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

static void fillTreeItemsWithMarks(QTreeWidget *p_tree,
                                   const QMap<QChar, VVim::Mark> &p_marks)
{
    p_tree->clear();
    for (auto const &mark : p_marks) {
        if (!mark.m_location.isValid()) {
            continue;
        }

        QStringList itemStr;
        itemStr << mark.m_name << QString::number(mark.m_location.m_blockNumber + 1)
                << QString::number(mark.m_location.m_positionInBlock) << mark.m_text;
        QTreeWidgetItem *item = new QTreeWidgetItem(p_tree, itemStr);
        item->setFlags(item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    }

    p_tree->resizeColumnToContents(0);
    p_tree->resizeColumnToContents(1);
    p_tree->resizeColumnToContents(2);
    p_tree->resizeColumnToContents(3);
}

void VVimIndicator::updateMarksTree(QWidget *p_widget)
{
    QTreeWidget *markTree = dynamic_cast<QTreeWidget *>(p_widget);
    if (!m_vim) {
        markTree->clear();
        return;
    }

    const QMap<QChar, VVim::Mark> &marks = m_vim->getMarks().getMarks();
    fillTreeItemsWithMarks(markTree, marks);
}
