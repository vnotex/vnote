#ifndef THEMEPAGE_H
#define THEMEPAGE_H

#include "settingspage.h"

namespace vnotex
{
    class ThemePage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit ThemePage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();
    };
}

#endif // THEMEPAGE_H
