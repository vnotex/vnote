#include "buildpage.h"

#include <QGridLayout>
#include <QPushButton>

#include <widgets/treewidget.h>
#include <core/vnotex.h>
#include <core/buildmgr.h>

#include "buildmgr.h"

using namespace vnotex;

BuildPage::BuildPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void BuildPage::setupUI()
{
    auto mainLayout = new QGridLayout(this);
    
    m_buildExplorer = new TreeWidget(this);
    TreeWidget::setupSingleColumnHeaderlessTree(m_buildExplorer, true, true);
    TreeWidget::showHorizontalScrollbar(m_buildExplorer);
    mainLayout->addWidget(m_buildExplorer, 0, 0, 3, 2);
    
    auto refreshBtn = new QPushButton(tr("Refresh"), this);
    mainLayout->addWidget(refreshBtn, 3, 0, 1, 1);
    
    auto openLocationBtn = new QPushButton(tr("Open Location"), this);
    mainLayout->addWidget(openLocationBtn, 3, 1, 1, 1);
    
    auto addBtn = new QPushButton(tr("Add"), this);
    mainLayout->addWidget(addBtn, 4, 0, 1, 1);
    
    auto deleteBtn = new QPushButton(tr("Delete"), this);
    mainLayout->addWidget(deleteBtn, 4, 1, 1, 1);
}

void BuildPage::setupBuild(QTreeWidgetItem *p_item, const BuildMgr::BuildInfo &p_info)
{
    qDebug() << "setupBuild: " << p_info.m_name;
    p_item->setText(0, p_info.m_displayName);
    p_item->setData(0, Qt::UserRole, QVariant::fromValue(p_info));
}

void BuildPage::loadBuilds()
{
    const auto &buildMgr = VNoteX::getInst().getBuildMgr();
    const auto &builds = buildMgr.getAllBuilds();
    
    m_buildExplorer->clear();
    for (const auto &info : builds) {
        addBuild(info);
    }
}

void BuildPage::addBuild(const BuildMgr::BuildInfo &p_info)
{
    auto item = new QTreeWidgetItem(m_buildExplorer);
    
    setupBuild(item, p_info);
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
