#ifndef MISCPAGE_H
#define MISCPAGE_H

#include "settingspage.h"

namespace vnotex
{
    class MiscPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit MiscPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();
    };
}

#endif // MISCPAGE_H
