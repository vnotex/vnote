#ifndef TEXTEDITORPAGE_H
#define TEXTEDITORPAGE_H

#include "settingspage.h"

class QComboBox;
class QCheckBox;
class QSpinBox;

namespace vnotex
{
    class TextEditorPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit TextEditorPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QComboBox *m_lineNumberComboBox = nullptr;

        QCheckBox *m_textFoldingCheckBox = nullptr;

        QComboBox *m_inputModeComboBox = nullptr;

        QComboBox *m_centerCursorComboBox = nullptr;

        QComboBox *m_wrapModeComboBox = nullptr;

        QCheckBox *m_expandTabCheckBox = nullptr;

        QSpinBox *m_tabStopWidthSpinBox = nullptr;

        QSpinBox *m_zoomDeltaSpinBox = nullptr;
    };
}

#endif // TEXTEDITORPAGE_H
