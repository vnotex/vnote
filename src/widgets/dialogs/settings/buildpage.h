#ifndef BUILDPAGE_H
#define BUILDPAGE_H

#include "settingspage.h"

class QTreeWidget;

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
        
        void loadBuilds();
        
        QTreeWidget *m_buildTreeWidget = nullptr;
    };
}

#endif // BUILDPAGE_H
