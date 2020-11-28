#ifndef MARKDOWNEDITORPAGE_H
#define MARKDOWNEDITORPAGE_H

#include "settingspage.h"

class QCheckBox;

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

        QCheckBox *m_insertFileNameAsTitleCheckBox = nullptr;

        QCheckBox *m_sectionNumberCheckBox = nullptr;

        QCheckBox *m_constrainImageWidthCheckBox = nullptr;

        QCheckBox *m_constrainInPlacePreviewWidthCheckBox = nullptr;

        QCheckBox *m_fetchImagesToLocalCheckBox = nullptr;
    };
}

#endif // MARKDOWNEDITORPAGE_H
