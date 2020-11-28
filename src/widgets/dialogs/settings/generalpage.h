#ifndef GENERALPAGE_H
#define GENERALPAGE_H

#include "settingspage.h"

class QComboBox;

namespace vnotex
{
    class GeneralPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit GeneralPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QComboBox *m_localeComboBox = nullptr;

        QComboBox *m_openGLComboBox = nullptr;
    };
}

#endif // GENERALPAGE_H
