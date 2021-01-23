#ifndef BUILDPAGE_H
#define BUILDPAGE_H

#include "settingspage.h"
#include "buildmgr.h"
// for BuildMgr::BuildInfo*

class QTreeWidget;
class QTreeWidgetItem;

namespace vnotex
{
    class BuildPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit BuildPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();
        
        void setupBuild(QTreeWidgetItem *p_item, const BuildMgr::BuildInfo &p_info);
        
        void loadBuilds();
        
        void addBuild(const BuildMgr::BuildInfo &p_info);
        
        QTreeWidget *m_buildExplorer = nullptr;
    };
}

#endif // BUILDPAGE_H
