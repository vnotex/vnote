#ifndef APPEARANCEPAGE_H
#define APPEARANCEPAGE_H

#include "settingspage.h"

class QCheckBox;
class QSpinBox;

namespace vnotex
{
    class AppearancePage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit AppearancePage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QCheckBox *m_systemTitleBarCheckBox = nullptr;

        QSpinBox *m_toolBarIconSizeSpinBox = nullptr;
    };
}

#endif // APPEARANCEPAGE_H
