#ifndef HTMLTEMPLATEHELPER_H
#define HTMLTEMPLATEHELPER_H

#include <QString>

namespace vnotex
{
    class MarkdownEditorConfig;
    struct WebResource;

    // Global options to be passed to Web side at the very beginning.
    struct WebGlobalOptions
    {
        bool m_webPlantUml = true;

        QString m_plantUmlWebService;

        bool m_webGraphviz = true;

        QString m_mathJaxScript;

        bool m_sectionNumberEnabled = true;

        int m_sectionNumberBaseLevel = 2;

        bool m_constrainImageWidthEnabled = true;

        bool m_imageAlignCenterEnabled = true;

        bool m_protectFromXss = false;

        bool m_htmlTagEnabled = true;

        bool m_autoBreakEnabled = false;

        bool m_linkifyEnabled = true;

        bool m_indentFirstLineEnabled = false;

        bool m_codeBlockLineNumberEnabled = true;

        // Force to use transparent background.
        bool m_transparentBackgroundEnabled = false;

        // Whether the content elements are scrollable. Like PDF, it is false.
        bool m_scrollable = true;

        int m_bodyWidth = -1;

        int m_bodyHeight = -1;

        // Whether transform inlie SVG to PNG.
        // For wkhtmltopdf converter, it could not render some inline SVG correctly.
        // This is just a hint not mandatory. For now, PlantUML and Graphviz needs this.
        bool m_transformSvgToPngEnabled = false;

        // wkhtmltopdf will make the MathJax formula too small.
        qreal m_mathJaxScale = -1;

        // Whether remove the tool bar of code blocks added by Prism.js.
        bool m_removeCodeToolBarEnabled = false;

        QString toJavascriptObject() const;
    };

    // Help to generate and update HTML templates.
    class HtmlTemplateHelper
    {
    public:
        struct Paras
        {
            QString m_webStyleSheetFile;

            QString m_highlightStyleSheetFile;

            bool m_transparentBackgroundEnabled = false;

            bool m_scrollable = true;

            int m_bodyWidth = -1;

            int m_bodyHeight = -1;

            bool m_transformSvgToPngEnabled = false;

            qreal m_mathJaxScale = -1;

            bool m_removeCodeToolBarEnabled = false;
        };

        HtmlTemplateHelper() = delete;

        static const QString &getMarkdownViewerTemplate();
        static void updateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config, bool p_force = false);

        static QString generateMarkdownViewerTemplate(const MarkdownEditorConfig &p_config, const Paras &p_paras);

        static QString generateExportTemplate(const MarkdownEditorConfig &p_config,
                                              bool p_addOutlinePanel);

        static void fillTitle(QString &p_template, const QString &p_title);

        static void fillStyleContent(QString &p_template, const QString &p_styles);

        static void fillHeadContent(QString &p_template, const QString &p_head);

        static void fillContent(QString &p_template, const QString &p_content);

        static void fillBodyClassList(QString &p_template, const QString &p_classList);

        static void fillOutlinePanel(QString &p_template, WebResource &p_exportResource, bool p_addOutlinePanel);

    private:
        static QString errorPage();

        struct Template
        {
            int m_revision = -1;
            QString m_template;
        };

        // Template for MarkdownViewer.
        static Template s_markdownViewerTemplate;
    };
}

#endif // HTMLTEMPLATEHELPER_H
