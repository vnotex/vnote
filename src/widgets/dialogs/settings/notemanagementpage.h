#ifndef NOTEMANAGEMENTPAGE_H
#define NOTEMANAGEMENTPAGE_H

#include "settingspage.h"

class QCheckBox;
class QComboBox;

namespace vnotex
{
    class NoteManagementPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit NoteManagementPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        bool saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QCheckBox *m_perNotebookHistoryCheckBox = nullptr;

        QComboBox *m_lineEndingComboBox = nullptr;

        QComboBox *m_defaultOpenModeComboBox = nullptr;
    };
}

#endif // NOTEMANAGEMENTPAGE_H
