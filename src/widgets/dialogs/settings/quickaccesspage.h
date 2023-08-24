#ifndef QUICKACCESSPAGE_H
#define QUICKACCESSPAGE_H

#include "settingspage.h"

#include <core/sessionconfig.h>

class QGroupBox;
class QPlainTextEdit;
class QComboBox;

namespace vnotex
{
    class LocationInputWithBrowseButton;
    class LineEditWithSnippet;
    class NoteTemplateSelector;

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

        QGroupBox *setupQuickNoteGroup();

        void newQuickNoteScheme();

        void removeQuickNoteScheme();

        void saveCurrentQuickNote();

        void loadCurrentQuickNote();

        void loadQuickNoteSchemes();

        void saveQuickNoteSchemes();

        void setCurrentQuickNote(int idx);

        static QString getDefaultQuickNoteFolderPath();

        LocationInputWithBrowseButton *m_flashPageInput = nullptr;

        QPlainTextEdit *m_quickAccessTextEdit = nullptr;

        QComboBox *m_quickNoteSchemeComboBox = nullptr;

        LocationInputWithBrowseButton *m_quickNoteFolderPathInput = nullptr;

        LineEditWithSnippet *m_quickNoteNoteNameLineEdit = nullptr;

        NoteTemplateSelector *m_quickNoteTemplateSelector = nullptr;

        QGroupBox *m_quickNoteInfoGroupBox = nullptr;

        QVector<SessionConfig::QuickNoteScheme> m_quickNoteSchemes;

        int m_quickNoteCurrentIndex = -1;
    };
}

#endif // QUICKACCESSPAGE_H
