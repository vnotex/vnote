#ifndef QUICKACCESSPAGE_H
#define QUICKACCESSPAGE_H

#include "settingspage.h"

class QCheckBox;
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

        bool saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QGroupBox *setupFlashPageGroup();

        QGroupBox *setupQuickAccessGroup();

        QGroupBox *setupQuickNotePageGroup();

        LocationInputWithBrowseButton *m_flashPageInput = nullptr;

        QPlainTextEdit *m_quickAccessTextEdit = nullptr;

        LocationInputWithBrowseButton *m_quickNoteStoragePath;

        QCheckBox *m_quickMarkdownCheckBox = nullptr;
        QCheckBox *m_quickTextCheckBox = nullptr;
        QCheckBox *m_quickMindmapCheckBox = nullptr;
    };
}

#endif // QUICKACCESSPAGE_H
