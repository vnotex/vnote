#ifndef EXPORTDATA_H
#define EXPORTDATA_H

#include <QSharedPointer>

class QPageLayout;

namespace vnotex {
enum class ExportSource { CurrentBuffer = 0, CurrentNote, CurrentFolder, CurrentNotebook };

enum class ExportFormat { Markdown = 0, HTML, PDF, Custom };

struct ExportHtmlOption {
  QJsonObject toJson() const;
  void fromJson(const QJsonObject &p_obj);

  bool operator==(const ExportHtmlOption &p_other) const;

  bool m_embedStyles = true;

  bool m_completePage = true;

  bool m_embedImages = true;

  bool m_useMimeHtmlFormat = false;

  // Whether add outline panel.
  bool m_addOutlinePanel = true;

  // When exporting to PDF or custom format, we may need to export to HTML first without scrollable.
  bool m_scrollable = true;
};

struct ExportPdfOption {
  ExportPdfOption();

  QJsonObject toJson() const;
  void fromJson(const QJsonObject &p_obj);

  bool operator==(const ExportPdfOption &p_other) const;

  QSharedPointer<QPageLayout> m_layout;

  // Add TOC at the front.
  bool m_addTableOfContents = false;

  bool m_useWkhtmltopdf = false;

  // Valid only when wkhtmltopdf is used.
  bool m_allInOne = false;

  QString m_wkhtmltopdfExePath;

  QString m_wkhtmltopdfArgs;
};

struct ExportCustomOption {
  QJsonObject toJson() const;
  void fromJson(const QJsonObject &p_obj);

  bool operator==(const ExportCustomOption &p_other) const;

  QString m_name;

  QString m_targetSuffix;

  QString m_command;

  bool m_useHtmlInput = true;

  bool m_allInOne = false;

  // Whether the page of target format is scrollable.
  bool m_targetPageScrollable = false;

  // The default value here follows the rules of Pandoc.
#if defined(Q_OS_WIN)
  QString m_resourcePathSeparator = ";";
#else
  QString m_resourcePathSeparator = ":";
#endif
};

struct ExportOption {
  QJsonObject toJson() const;
  void fromJson(const QJsonObject &p_obj);

  bool operator==(const ExportOption &p_other) const;

  ExportSource m_source = ExportSource::CurrentBuffer;

  ExportFormat m_targetFormat = ExportFormat::HTML;

  bool m_useTransparentBg = true;

  QString m_renderingStyleFile;

  QString m_syntaxHighlightStyleFile;

  QString m_outputDir;

  bool m_recursive = true;

  bool m_exportAttachments = true;

  ExportHtmlOption m_htmlOption;

  ExportPdfOption m_pdfOption;

  QString m_customExport;

  // Following fields are used in runtime only.
  ExportCustomOption *m_customOption = nullptr;

  bool m_transformSvgToPngEnabled = false;

  bool m_removeCodeToolBarEnabled = true;
};

inline QString exportFormatString(ExportFormat p_format) {
  switch (p_format) {
  case ExportFormat::Markdown:
    return QStringLiteral("Markdown");

  case ExportFormat::HTML:
    return QStringLiteral("HTML");

  case ExportFormat::PDF:
    return QStringLiteral("PDF");

  case ExportFormat::Custom:
    return QStringLiteral("Custom");
  }

  return QStringLiteral("Unknown");
}
} // namespace vnotex

#endif // EXPORTDATA_H
