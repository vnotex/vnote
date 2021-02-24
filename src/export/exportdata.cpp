#include "exportdata.h"

#include <QPageLayout>
#include <QJsonObject>
#include <QJsonArray>

using namespace vnotex;

QJsonObject ExportHtmlOption::toJson() const
{
    QJsonObject obj;
    obj["embed_styles"] = m_embedStyles;
    obj["complete_page"] = m_completePage;
    obj["embed_images"] = m_embedImages;
    obj["use_mime_html_format"] = m_useMimeHtmlFormat;
    obj["add_outline_panel"] = m_addOutlinePanel;
    return obj;
}

void ExportHtmlOption::fromJson(const QJsonObject &p_obj)
{
    if (p_obj.isEmpty()) {
        return;
    }

    m_embedStyles = p_obj["embed_styles"].toBool();
    m_completePage = p_obj["complete_page"].toBool();
    m_embedImages = p_obj["embed_images"].toBool();
    m_useMimeHtmlFormat = p_obj["use_mime_html_format"].toBool();
    m_addOutlinePanel = p_obj["add_outline_panel"].toBool();
}

bool ExportHtmlOption::operator==(const ExportHtmlOption &p_other) const
{
    return m_embedStyles == p_other.m_embedStyles
           && m_completePage == p_other.m_completePage
           && m_embedImages == p_other.m_embedImages
           && m_useMimeHtmlFormat == p_other.m_useMimeHtmlFormat
           && m_addOutlinePanel == p_other.m_addOutlinePanel;
}

ExportPdfOption::ExportPdfOption()
    : m_layout(new QPageLayout(QPageSize(QPageSize::A4),
                               QPageLayout::Portrait,
                               QMarginsF(10, 16, 10, 10),
                               QPageLayout::Millimeter))
{
}

QJsonObject ExportPdfOption::toJson() const
{
    QJsonObject obj;
    obj["add_table_of_contents"] = m_addTableOfContents;
    obj["use_wkhtmltopdf"] = m_useWkhtmltopdf;
    obj["wkhtmltopdf_exe_path"] = m_wkhtmltopdfExePath;
    obj["wkhtmltopdf_args"] = m_wkhtmltopdfArgs;
    return obj;
}

void ExportPdfOption::fromJson(const QJsonObject &p_obj)
{
    if (p_obj.isEmpty()) {
        return;
    }

    m_addTableOfContents = p_obj["add_table_of_contents"].toBool();
    m_useWkhtmltopdf = p_obj["use_wkhtmltopdf"].toBool();
    m_wkhtmltopdfExePath = p_obj["wkhtmltopdf_exe_path"].toString();
    m_wkhtmltopdfArgs = p_obj["wkhtmltopdf_args"].toString();
}

bool ExportPdfOption::operator==(const ExportPdfOption &p_other) const
{
    return m_addTableOfContents == p_other.m_addTableOfContents
           && m_useWkhtmltopdf == p_other.m_useWkhtmltopdf
           && m_wkhtmltopdfExePath == p_other.m_wkhtmltopdfExePath
           && m_wkhtmltopdfArgs == p_other.m_wkhtmltopdfArgs;
}

QJsonObject ExportOption::toJson() const
{
    QJsonObject obj;
    obj["target_format"] = static_cast<int>(m_targetFormat);
    obj["use_transparent_bg"] = m_useTransparentBg;
    obj["output_dir"] = m_outputDir;
    obj["recursive"] = m_recursive;
    obj["export_attachments"] = m_exportAttachments;
    obj["html_option"] = m_htmlOption.toJson();
    obj["pdf_option"] = m_pdfOption.toJson();
    return obj;
}

void ExportOption::fromJson(const QJsonObject &p_obj)
{
    if (p_obj.isEmpty()) {
        return;
    }

    {
        int fmt = p_obj["target_format"].toInt();
        switch (fmt) {
        case static_cast<int>(ExportFormat::Markdown):
            m_targetFormat = ExportFormat::Markdown;
            break;

        case static_cast<int>(ExportFormat::PDF):
            m_targetFormat = ExportFormat::PDF;
            break;

        case static_cast<int>(ExportFormat::Custom):
            m_targetFormat = ExportFormat::Custom;
            break;

        case static_cast<int>(ExportFormat::HTML):
            Q_FALLTHROUGH();
        default:
            m_targetFormat = ExportFormat::HTML;
            break;
        }
    }

    m_useTransparentBg = p_obj["use_transparent_bg"].toBool();
    m_outputDir = p_obj["output_dir"].toString();
    m_recursive = p_obj["recursive"].toBool();
    m_exportAttachments = p_obj["export_attachments"].toBool();
    m_htmlOption.fromJson(p_obj["html_option"].toObject());
    m_pdfOption.fromJson(p_obj["pdf_option"].toObject());
}

bool ExportOption::operator==(const ExportOption &p_other) const
{
    bool ret = m_targetFormat == p_other.m_targetFormat
               && m_useTransparentBg == p_other.m_useTransparentBg
               && m_outputDir == p_other.m_outputDir
               && m_recursive == p_other.m_recursive
               && m_exportAttachments == p_other.m_exportAttachments;

    if (!ret) {
        return false;
    }

    if (!(m_htmlOption == p_other.m_htmlOption)) {
        return false;
    }

    if (!(m_pdfOption == p_other.m_pdfOption)) {
        return false;
    }

    return true;
}
