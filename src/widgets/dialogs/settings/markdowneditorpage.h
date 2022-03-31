#ifndef MARKDOWNEDITORPAGE_H
#define MARKDOWNEDITORPAGE_H

#include "settingspage.h"

class QCheckBox;
class QGroupBox;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;

namespace vnotex
{
    class LocationInputWithBrowseButton;

    class MarkdownEditorPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit MarkdownEditorPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        bool saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        QGroupBox *setupGeneralGroup();

        QGroupBox *setupReadGroup();

        QGroupBox *setupEditGroup();

        QCheckBox *m_insertFileNameAsTitleCheckBox = nullptr;

        QCheckBox *m_constrainImageWidthCheckBox = nullptr;

        QCheckBox *m_imageAlignCenterCheckBox = nullptr;

        QCheckBox *m_constrainInplacePreviewWidthCheckBox = nullptr;

        QCheckBox *m_inplacePreviewSourceImageLinkCheckBox = nullptr;

        QCheckBox *m_inplacePreviewSourceCodeBlockCheckBox = nullptr;

        QCheckBox *m_inplacePreviewSourceMathCheckBox = nullptr;

        QCheckBox *m_fetchImagesToLocalCheckBox = nullptr;

        QCheckBox *m_htmlTagCheckBox = nullptr;

        QCheckBox *m_autoBreakCheckBox = nullptr;

        QCheckBox *m_linkifyCheckBox = nullptr;

        QCheckBox *m_indentFirstLineCheckBox = nullptr;

        QCheckBox *m_codeBlockLineNumberCheckBox = nullptr;

        QDoubleSpinBox *m_zoomFactorSpinBox = nullptr;

        QComboBox *m_sectionNumberComboBox = nullptr;

        QSpinBox *m_sectionNumberBaseLevelSpinBox = nullptr;

        QComboBox *m_sectionNumberStyleComboBox = nullptr;

        QCheckBox *m_smartTableCheckBox = nullptr;

        QCheckBox *m_spellCheckCheckBox = nullptr;

        QComboBox *m_plantUmlModeComboBox = nullptr;

        LocationInputWithBrowseButton *m_plantUmlJarFileInput = nullptr;

        QLineEdit *m_plantUmlWebServiceLineEdit = nullptr;

        QComboBox *m_graphvizModeComboBox = nullptr;

        LocationInputWithBrowseButton *m_graphvizFileInput = nullptr;

        QLineEdit *m_mathJaxScriptLineEdit = nullptr;

        QCheckBox *m_editorOverriddenFontFamilyCheckBox = nullptr;

        QFontComboBox *m_editorOverriddenFontFamilyComboBox = nullptr;

        QCheckBox *m_richPasteByDefaultCheckBox = nullptr;
    };
}

#endif // MARKDOWNEDITORPAGE_H
