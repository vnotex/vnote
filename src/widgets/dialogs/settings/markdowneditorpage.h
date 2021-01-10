#ifndef MARKDOWNEDITORPAGE_H
#define MARKDOWNEDITORPAGE_H

#include "settingspage.h"

class QCheckBox;
class QGroupBox;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;

namespace vnotex
{
    class MarkdownEditorPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit MarkdownEditorPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QGroupBox *setupGeneralGroup();

        QGroupBox *setupReadGroup();

        QGroupBox *setupEditGroup();

        QCheckBox *m_insertFileNameAsTitleCheckBox = nullptr;

        QCheckBox *m_constrainImageWidthCheckBox = nullptr;

        QCheckBox *m_constrainInPlacePreviewWidthCheckBox = nullptr;

        QCheckBox *m_fetchImagesToLocalCheckBox = nullptr;

        QCheckBox *m_htmlTagCheckBox = nullptr;

        QCheckBox *m_autoBreakCheckBox = nullptr;

        QCheckBox *m_linkifyCheckBox = nullptr;

        QCheckBox *m_indentFirstLineCheckBox = nullptr;

        QDoubleSpinBox *m_zoomFactorSpinBox = nullptr;

        QComboBox *m_sectionNumberComboBox = nullptr;

        QSpinBox *m_sectionNumberBaseLevelSpinBox = nullptr;

        QComboBox *m_sectionNumberStyleComboBox = nullptr;

        QCheckBox *m_smartTableCheckBox = nullptr;
    };
}

#endif // MARKDOWNEDITORPAGE_H
