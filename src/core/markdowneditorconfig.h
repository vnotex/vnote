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

        bool getSectionNumberEnabled() const;
        void setSectionNumberEnabled(bool p_enabled);

        bool getConstrainImageWidthEnabled() const;
        void setConstrainImageWidthEnabled(bool p_enabled);

        bool getConstrainInPlacePreviewWidthEnabled() const;
        void setConstrainInPlacePreviewWidthEnabled(bool p_enabled);

        qreal getZoomFactorInReadMode() const;
        void setZoomFactorInReadMode(qreal p_factor);

        bool getFetchImagesInParseAndPaste() const;
        void setFetchImagesInParseAndPaste(bool p_enabled);

        bool getProtectFromXss() const;

    private:
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
        bool m_sectionNumberEnabled = true;

        // Whether enable image width constraint.
        bool m_constrainImageWidthEnabled = true;

        // Whether enable in-place preview width constraint.
        bool m_constrainInPlacePreviewWidthEnabled = false;

        qreal m_zoomFactorInReadMode = 1.0;

        // Whether fetch images to local in Parse To Markdown And Paste.
        bool m_fetchImagesInParseAndPaste = true;

        // Whether protect from Cross-Site Scripting.
        bool m_protectFromXss = false;
    };
}

#endif // MARKDOWNEDITORCONFIG_H
