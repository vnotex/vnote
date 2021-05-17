#ifndef QUICKACCESSPAGE_H
#define QUICKACCESSPAGE_H

#include "settingspage.h"

class QGroupBox;
class QPlainTextEdit;

namespace vnotex
{
    class LocationInputWithBrowseButton;

    class QuickAccessPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit QuickAccessPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QGroupBox *setupFlashPageGroup();

        QGroupBox *setupQuickAccessGroup();

        LocationInputWithBrowseButton *m_flashPageInput = nullptr;

        QPlainTextEdit *m_quickAccessTextEdit = nullptr;
    };
}

#endif // QUICKACCESSPAGE_H
