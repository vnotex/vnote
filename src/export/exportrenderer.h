#ifndef EXPORTRENDERER_H
#define EXPORTRENDERER_H

#include <QFlags>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include "exportdata.h"

class QWidget;

namespace vnotex {
class ServiceLocator;

struct ExportFileInfo {
  QString filePath;
  QString fileName;
  QString resourcePath;
  QString attachmentFolderPath;
  QString fileTypeName = QStringLiteral("Others");
};

struct ExportInput {
  ExportFileInfo file;
  QString content;
};

enum class ExportPdfBackend { Html, Native };

enum class ExportOptionGroup {
  WebStyles = 0x1,
  TransparentBackground = 0x2,
  Html = 0x4,
  PdfLayout = 0x8,
  PdfToc = 0x10,
  Wkhtmltopdf = 0x20,
  CustomScrollable = 0x40
};
Q_DECLARE_FLAGS(ExportOptionGroups, ExportOptionGroup);

struct ExportTargetCapability {
  ExportFormat format;
  // Empty source suffix retains the input filename; Custom uses its scheme's suffix.
  QString suffix;
  ExportOptionGroups options;
  ExportPdfBackend pdfBackend = ExportPdfBackend::Html;
};

struct ExportCapabilities {
  QVector<ExportTargetCapability> targets;
};

enum class ExportHtmlPurpose { Standalone, Pdf, Custom };

struct ExportHtmlRequest {
  ExportOption options;
  ExportHtmlPurpose purpose = ExportHtmlPurpose::Standalone;
};

class ExportRenderer : public QObject {
  Q_OBJECT
public:
  ExportRenderer(ServiceLocator &p_services, QWidget *p_widgetParent, QObject *p_parent);
  ~ExportRenderer() override;

  virtual bool renderSource(const ExportInput &p_input, ExportFormat p_format,
                            const QString &p_outputFile) = 0;
  virtual bool renderPdf(const ExportInput &p_input, const ExportOption &p_option,
                         const QString &p_outputFile);
  virtual bool renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                          const QString &p_outputFile) = 0;
  virtual void stop() = 0;

signals:
  void logRequested(const QString &p_log);

protected:
  ServiceLocator &m_services;
  QPointer<QWidget> m_widgetParent;
};
} // namespace vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::ExportOptionGroups)

#endif // EXPORTRENDERER_H
