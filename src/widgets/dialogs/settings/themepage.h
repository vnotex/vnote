#ifndef THEMEPAGE_H
#define THEMEPAGE_H

#include "settingspage.h"

class QListWidget;
class QLabel;

namespace vnotex
{
    class ThemePage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit ThemePage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void loadThemes();

        void loadThemePreview(const QString &p_name);

        QString currentTheme() const;

        QListWidget *m_themeListWidget = nullptr;

        QLabel *m_previewLabel = nullptr;

        QString m_noPreviewText;
    };
}

#endif // THEMEPAGE_H
