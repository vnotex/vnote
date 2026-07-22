#ifndef EXPORTDATA_H
#define EXPORTDATA_H

#include <QSharedPointer>

class QPageLayout;

namespace vnotex {
enum class ExportSource {
  CurrentBuffer = 0,
  CurrentNote,
  CurrentFolder,
  CurrentNotebook,
  Workspace
};

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

  // Running header/footer text (wkhtmltopdf path only). Support wkhtmltopdf
  // placeholders such as [page], [topage], [title], [date].
  QString m_headerLeft;
  QString m_headerCenter;
  QString m_headerRight;
  QString m_footerLeft;
  QString m_footerCenter;
  // Defaults to "[page]" in the ctor to preserve the historical page-number footer.
  QString m_footerRight;
};

struct ExportCustomOption {
  QJsonObject toJson() const;
  void fromJson(const QJsonObject &p_obj);

  bool operator==(const ExportCustomOption &p_other) const;

  // Read-only built-in schemes shipped in code (Docx via pandoc, PNG via
  // wkhtmltoimage). Always present in the dialog, never persisted to session config.
  static QVector<ExportCustomOption> builtinSchemes();

  // Prepend builtinSchemes() to p_options in place, skipping any built-in whose
  // m_name already exists among the loaded user schemes (user scheme wins /
  // clone-to-edit upgrade path). Used by the Export dialog on load.
  static void seedBuiltins(QVector<ExportCustomOption> &p_options);

  // Return a copy of p_options with all m_builtin entries removed. Used by the
  // Export dialog before persisting so built-ins are never written to config.
  static QVector<ExportCustomOption> withoutBuiltins(const QVector<ExportCustomOption> &p_options);

  QString m_name;

  QString m_targetSuffix;

  QString m_command;

  bool m_useHtmlInput = true;

  bool m_allInOne = false;

  // Whether the page of target format is scrollable.
  bool m_targetPageScrollable = false;

  // Runtime-only: true for shipped built-in schemes. Not persisted (excluded from
  // toJson/fromJson and operator==), used by the dialog to make fields read-only.
  bool m_builtin = false;

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

  // Runtime-only: identifies the workspace to export when m_source ==
  // ExportSource::Workspace. Intentionally NOT serialized (toJson/fromJson) so a
  // workspace can never become the sticky default source.
  QString m_workspaceId;

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

  // Defaults to false so direct HTML export keeps the code-block toolbar (copy button).
  // The intermediate HTML feeding PDF/custom explicitly sets this true
  // (Exporter::getExportOptionForIntermediateHtml), and WebViewExporter::prepare() forces
  // it true for non-HTML target formats (PDF/Custom), so those paths still drop the toolbar.
  bool m_removeCodeToolBarEnabled = false;
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
