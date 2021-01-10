#ifndef MARKDOWNEDITORCONFIG_H
#define MARKDOWNEDITORCONFIG_H

#include "iconfig.h"

#include "viewerresource.h"

#include <QSharedPointer>
#include <QVector>

namespace vnotex
{
    class TextEditorConfig;

    class MarkdownEditorConfig : public IConfig
    {
    public:
        enum SectionNumberMode
        {
            None,
            Read,
            Edit
        };

        enum SectionNumberStyle
        {
            // 1.1.
            DigDotDigDot,
            // 1.1
            DigDotDig
        };

        MarkdownEditorConfig(ConfigMgr *p_mgr,
                             IConfig *p_topConfig,
                             const QSharedPointer<TextEditorConfig> &p_textEditorConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        void loadViewerResource(const QJsonObject &p_app, const QJsonObject &p_user);
        QJsonObject saveViewerResource() const;

        int revision() const Q_DECL_OVERRIDE;

        TextEditorConfig &getTextEditorConfig();
        const TextEditorConfig &getTextEditorConfig() const;

        const ViewerResource &getViewerResource() const;

        bool getWebPlantUml() const;

        bool getWebGraphviz() const;

        bool getPrependDotInRelativeLink() const;

        bool getConfirmBeforeClearObsoleteImages() const;
        void setConfirmBeforeClearObsoleteImages(bool p_confirm);

        bool getInsertFileNameAsTitle() const;
        void setInsertFileNameAsTitle(bool p_enabled);

        SectionNumberMode getSectionNumberMode() const;
        void setSectionNumberMode(SectionNumberMode p_mode);

        int getSectionNumberBaseLevel() const;
        void setSectionNumberBaseLevel(int p_level);

        SectionNumberStyle getSectionNumberStyle() const;
        void setSectionNumberStyle(SectionNumberStyle p_style);

        bool getConstrainImageWidthEnabled() const;
        void setConstrainImageWidthEnabled(bool p_enabled);

        bool getConstrainInPlacePreviewWidthEnabled() const;
        void setConstrainInPlacePreviewWidthEnabled(bool p_enabled);

        qreal getZoomFactorInReadMode() const;
        void setZoomFactorInReadMode(qreal p_factor);

        bool getFetchImagesInParseAndPaste() const;
        void setFetchImagesInParseAndPaste(bool p_enabled);

        bool getProtectFromXss() const;

        bool getHtmlTagEnabled() const;
        void setHtmlTagEnabled(bool p_enabled);

        bool getAutoBreakEnabled() const;
        void setAutoBreakEnabled(bool p_enabled);

        bool getLinkifyEnabled() const;
        void setLinkifyEnabled(bool p_enabled);

        bool getIndentFirstLineEnabled() const;
        void setIndentFirstLineEnabled(bool p_enabled);

        bool getSmartTableEnabled() const;
        void setSmartTableEnabled(bool p_enabled);

        int getSmartTableInterval() const;

    private:
        QString sectionNumberModeToString(SectionNumberMode p_mode) const;
        SectionNumberMode stringToSectionNumberMode(const QString &p_str) const;

        QString sectionNumberStyleToString(SectionNumberStyle p_style) const;
        SectionNumberStyle stringToSectionNumberStyle(const QString &p_str) const;

        QSharedPointer<TextEditorConfig> m_textEditorConfig;

        ViewerResource m_viewerResource;

        // Whether use javascript or external program to render PlantUML.
        bool m_webPlantUml = true;

        bool m_webGraphviz = true;

        // Whether prepend a dot in front of the relative link, like images.
        bool m_prependDotInRelativeLink = false;

        // Whether ask for user confirmation before clearing obsolete images.
        bool m_confirmBeforeClearObsoleteImages = true;

        // Whether insert the name of the new file as title.
        bool m_insertFileNameAsTitle = true;

        // Whether enable section numbering.
        SectionNumberMode m_sectionNumberMode = SectionNumberMode::Read;

        // 1 based.
        int m_sectionNumberBaseLevel = 2;

        // Section number style.
        SectionNumberStyle m_sectionNumberStyle = SectionNumberStyle::DigDotDigDot;

        // Whether enable image width constraint.
        bool m_constrainImageWidthEnabled = true;

        // Whether enable in-place preview width constraint.
        bool m_constrainInPlacePreviewWidthEnabled = false;

        qreal m_zoomFactorInReadMode = 1.0;

        // Whether fetch images to local in Parse To Markdown And Paste.
        bool m_fetchImagesInParseAndPaste = true;

        // Whether protect from Cross-Site Scripting.
        bool m_protectFromXss = false;

        // Whether allow HTML tag in Markdown source.
        bool m_htmlTagEnabled = true;

        // Whether auto break a line with `\n`.
        bool m_autoBreakEnabled = false;

        // Whether convert URL-like text to links.
        bool m_linkifyEnabled = true;

        // Whether indent the first line of a paragraph.
        bool m_indentFirstLineEnabled = false;

        bool m_smartTableEnabled = true;

        // Interval time to do smart table format.
        int m_smartTableInterval = 2000;
    };
}

#endif // MARKDOWNEDITORCONFIG_H
