#include "buildpage.h"

#include <QGridLayout>
#include <QPushButton>

#include <widgets/treewidget.h>

using namespace vnotex;

BuildPage::BuildPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void BuildPage::setupUI()
{
    auto mainLayout = new QGridLayout(this);
    
    m_buildTreeWidget = new TreeWidget(this);
    TreeWidget::setupSingleColumnHeaderlessTree(m_buildTreeWidget, true, true);
    TreeWidget::showHorizontalScrollbar(m_buildTreeWidget);
    mainLayout->addWidget(m_buildTreeWidget, 0, 0, 3, 2);
    
    auto refreshBtn = new QPushButton(tr("Refresh"), this);
    mainLayout->addWidget(refreshBtn, 3, 0, 1, 1);
    
    auto openLocationBtn = new QPushButton(tr("Open Location"), this);
    mainLayout->addWidget(openLocationBtn, 3, 1, 1, 1);
    
    auto addBtn = new QPushButton(tr("Add"), this);
    mainLayout->addWidget(addBtn, 4, 0, 1, 1);
    
    auto deleteBtn = new QPushButton(tr("Delete"), this);
    mainLayout->addWidget(deleteBtn, 4, 1, 1, 1);
}

void BuildPage::loadBuilds()
{
    m_buildTreeWidget->clear();
}

void BuildPage::loadInternal()
{
    loadBuilds();
}

void BuildPage::saveInternal()
{
}

QString BuildPage::title() const
{
    return tr("Build");
}
