#ifndef EXPORTRENDERER_H
#define EXPORTRENDERER_H

#include <QObject>
#include <QPointer>
#include <QString>

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

struct ExportMarkdown {
  QString content;
  QString fileName;
  bool copyMedia = false;
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

  virtual bool renderMarkdown(const ExportInput &p_input, ExportMarkdown &p_output) = 0;
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

#endif // EXPORTRENDERER_H
