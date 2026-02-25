#include "exportdata.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPageLayout>

using namespace vnotex;

QJsonObject ExportHtmlOption::toJson() const {
  QJsonObject obj;
  obj["embedStyles"] = m_embedStyles;
  obj["completePage"] = m_completePage;
  obj["embedImages"] = m_embedImages;
  obj["useMimeHtmlFormat"] = m_useMimeHtmlFormat;
  obj["addOutlinePanel"] = m_addOutlinePanel;
  obj["scrollable"] = m_scrollable;
  return obj;
}

void ExportHtmlOption::fromJson(const QJsonObject &p_obj) {
  if (p_obj.isEmpty()) {
    return;
  }

  m_embedStyles = p_obj["embedStyles"].toBool();
  m_completePage = p_obj["completePage"].toBool();
  m_embedImages = p_obj["embedImages"].toBool();
  m_useMimeHtmlFormat = p_obj["useMimeHtmlFormat"].toBool();
  m_addOutlinePanel = p_obj["addOutlinePanel"].toBool();
  m_scrollable = p_obj["scrollable"].toBool(true);
}

bool ExportHtmlOption::operator==(const ExportHtmlOption &p_other) const {
  return m_embedStyles == p_other.m_embedStyles && m_completePage == p_other.m_completePage &&
         m_embedImages == p_other.m_embedImages &&
         m_useMimeHtmlFormat == p_other.m_useMimeHtmlFormat &&
         m_addOutlinePanel == p_other.m_addOutlinePanel && m_scrollable == p_other.m_scrollable;
}

static QJsonObject pageLayoutToJsonObject(const QPageLayout &p_layout) {
  QJsonObject obj;
  obj["pageSize"] = static_cast<int>(p_layout.pageSize().id());
  obj["orientation"] = static_cast<int>(p_layout.orientation());
  obj["units"] = static_cast<int>(p_layout.units());
  {
    QStringList marginsStr;
    const auto margins = p_layout.margins();
    marginsStr << QString::number(margins.left());
    marginsStr << QString::number(margins.top());
    marginsStr << QString::number(margins.right());
    marginsStr << QString::number(margins.bottom());
    obj["margins"] = marginsStr.join(QLatin1Char(','));
  }
  return obj;
}

static void jsonObjectToPageLayout(const QJsonObject &p_obj, QPageLayout &p_layout) {
  const int pageSize = p_obj["pageSize"].toInt(static_cast<int>(QPageSize::A4));
  p_layout.setPageSize(QPageSize(static_cast<QPageSize::PageSizeId>(pageSize)));

  const int orientation = p_obj["orientation"].toInt(static_cast<int>(QPageLayout::Portrait));
  p_layout.setOrientation(static_cast<QPageLayout::Orientation>(orientation));

  const int units = p_obj["units"].toInt(static_cast<int>(QPageLayout::Millimeter));
  p_layout.setUnits(static_cast<QPageLayout::Unit>(units));

  auto marginsStr = p_obj["margins"].toString().split(QLatin1Char(','));
  if (marginsStr.size() == 4) {
    p_layout.setMargins(QMarginsF(marginsStr[0].toDouble(), marginsStr[1].toDouble(),
                                  marginsStr[2].toDouble(), marginsStr[3].toDouble()));
  }
}

ExportPdfOption::ExportPdfOption()
    : m_layout(new QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                               QMarginsF(10, 16, 10, 10), QPageLayout::Millimeter)) {}

QJsonObject ExportPdfOption::toJson() const {
  QJsonObject obj;
  obj["addTableOfContents"] = m_addTableOfContents;
  obj["useWkhtmltopdf"] = m_useWkhtmltopdf;
  obj["allInOne"] = m_allInOne;
  obj["wkhtmltopdfExePath"] = m_wkhtmltopdfExePath;
  obj["wkhtmltopdfArgs"] = m_wkhtmltopdfArgs;
  obj["layout"] = pageLayoutToJsonObject(*m_layout);
  return obj;
}

void ExportPdfOption::fromJson(const QJsonObject &p_obj) {
  if (p_obj.isEmpty()) {
    return;
  }

  m_addTableOfContents = p_obj["addTableOfContents"].toBool();
  m_useWkhtmltopdf = p_obj["useWkhtmltopdf"].toBool();
  m_allInOne = p_obj["allInOne"].toBool();
  m_wkhtmltopdfExePath = p_obj["wkhtmltopdfExePath"].toString();
  m_wkhtmltopdfArgs = p_obj["wkhtmltopdfArgs"].toString();
  jsonObjectToPageLayout(p_obj["layout"].toObject(), *m_layout);
}

bool ExportPdfOption::operator==(const ExportPdfOption &p_other) const {
  return m_addTableOfContents == p_other.m_addTableOfContents &&
         m_useWkhtmltopdf == p_other.m_useWkhtmltopdf && m_allInOne == p_other.m_allInOne &&
         m_wkhtmltopdfExePath == p_other.m_wkhtmltopdfExePath &&
         m_wkhtmltopdfArgs == p_other.m_wkhtmltopdfArgs;
}

QJsonObject ExportCustomOption::toJson() const {
  QJsonObject obj;
  obj["name"] = m_name;
  obj["targetSuffix"] = m_targetSuffix;
  obj["command"] = m_command;
  obj["useHtmlInput"] = m_useHtmlInput;
  obj["allInOne"] = m_allInOne;
  obj["targetPageScrollable"] = m_targetPageScrollable;
  obj["resourcePathSeparator"] = m_resourcePathSeparator;
  return obj;
}

void ExportCustomOption::fromJson(const QJsonObject &p_obj) {
  if (p_obj.isEmpty()) {
    return;
  }

  m_name = p_obj["name"].toString();
  m_targetSuffix = p_obj["targetSuffix"].toString();
  m_command = p_obj["command"].toString();
  m_useHtmlInput = p_obj["useHtmlInput"].toBool();
  m_allInOne = p_obj["allInOne"].toBool();
  m_targetPageScrollable = p_obj["targetPageScrollable"].toBool();
  m_resourcePathSeparator = p_obj["resourcePathSeparator"].toString();
}

bool ExportCustomOption::operator==(const ExportCustomOption &p_other) const {
  return m_name == p_other.m_name && m_useHtmlInput == p_other.m_useHtmlInput &&
         m_targetSuffix == p_other.m_targetSuffix && m_command == p_other.m_command &&
         m_allInOne == p_other.m_allInOne &&
         m_targetPageScrollable == p_other.m_targetPageScrollable &&
         m_resourcePathSeparator == p_other.m_resourcePathSeparator;
}

QJsonObject ExportOption::toJson() const {
  QJsonObject obj;
  obj["targetFormat"] = static_cast<int>(m_targetFormat);
  obj["useTransparentBg"] = m_useTransparentBg;
  obj["outputDir"] = m_outputDir;
  obj["recursive"] = m_recursive;
  obj["exportAttachments"] = m_exportAttachments;
  obj["htmlOption"] = m_htmlOption.toJson();
  obj["pdfOption"] = m_pdfOption.toJson();
  obj["customExport"] = m_customExport;
  return obj;
}

void ExportOption::fromJson(const QJsonObject &p_obj) {
  if (p_obj.isEmpty()) {
    return;
  }

  {
    int fmt = p_obj["targetFormat"].toInt();
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

  m_useTransparentBg = p_obj["useTransparentBg"].toBool();
  m_outputDir = p_obj["outputDir"].toString();
  m_recursive = p_obj["recursive"].toBool();
  m_exportAttachments = p_obj["exportAttachments"].toBool();
  m_htmlOption.fromJson(p_obj["htmlOption"].toObject());
  m_pdfOption.fromJson(p_obj["pdfOption"].toObject());
  m_customExport = p_obj["customExport"].toString();
}

bool ExportOption::operator==(const ExportOption &p_other) const {
  bool ret = m_targetFormat == p_other.m_targetFormat &&
             m_useTransparentBg == p_other.m_useTransparentBg &&
             m_outputDir == p_other.m_outputDir && m_recursive == p_other.m_recursive &&
             m_exportAttachments == p_other.m_exportAttachments;

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
