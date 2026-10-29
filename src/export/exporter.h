#ifndef EXPORTER_H
#define EXPORTER_H

#include <QObject>
#include <QStringList>
#include <QVector>

#include "exportrenderer.h"
#include "exportrendererfactory.h"
#include <core/servicelocator.h>

namespace vnotex {
class HtmlPdfExporter;

class Exporter : public QObject {
  Q_OBJECT
public:
  Exporter(ServiceLocator &p_services, ExportRendererFactory p_renderers, QWidget *p_parent);

  QString doExportFile(const ExportOption &p_option, const QString &p_content,
                       const QString &p_filePath, const QString &p_fileName,
                       const QString &p_resourcePath, const QString &p_attachmentFolderPath,
                       const QString &p_fileTypeName);
  QStringList doExportBatch(const ExportOption &p_option, const QVector<ExportFileInfo> &p_files,
                            const QString &p_batchName);
  void stop();

signals:
  void progressUpdated(int p_val, int p_maximum);
  void logRequested(const QString &p_log);
  void askedToStop();

private:
  bool loadInput(const ExportFileInfo &p_file, const QString *p_suppliedContent,
                 ExportInput &p_input);
  bool validateTarget(const ExportOption &p_option, const QString &p_fileTypeName);
  QString doExport(const ExportOption &p_option, const QString &p_outputDir,
                   const ExportInput &p_input);
  QString doExportSource(const ExportOption &p_option, const QString &p_outputDir,
                         const ExportInput &p_input);
  QString doExportHtml(const ExportHtmlRequest &p_request, const QString &p_outputDir,
                       const ExportInput &p_input);
  QString doExportPdf(const ExportOption &p_option, const QString &p_outputDir,
                      const ExportInput &p_input);
  QString doExportCustom(const ExportOption &p_option, const QString &p_outputDir,
                         const ExportInput &p_input);
  bool doExportCustom(const ExportOption &p_option, const QStringList &p_files,
                      const QStringList &p_resourcePaths, const QString &p_filePath);
  QString doExportPdfAllInOne(const ExportOption &p_option, const QVector<ExportFileInfo> &p_files,
                              const QString &p_batchName);
  QString doExportCustomAllInOne(const ExportOption &p_option,
                                 const QVector<ExportFileInfo> &p_files,
                                 const QString &p_batchName);
  QStringList renderBatchHtml(const ExportHtmlRequest &p_request,
                              const QVector<ExportFileInfo> &p_files, const QString &p_outputDir);
  void exportAttachments(const QString &p_attachmentFolderPath, const QString &p_srcFilePath,
                         const QString &p_outputFolder, const QString &p_destFilePath);
  ExportRenderer *renderer(const QString &p_fileTypeName);
  HtmlPdfExporter *pdfExporter();
  void cleanUp();
  bool checkAskedToStop() const;

  static ExportHtmlRequest getExportOptionForIntermediateHtml(const ExportOption &p_option,
                                                              const QString &p_outputDir);
  static QString outputPath(const ExportInput &p_input, const QString &p_outputDir,
                            const QString &p_suffix);
  static QString evaluateCommand(const ExportOption &p_option, const QStringList &p_files,
                                 const QStringList &p_resourcePaths, const QString &p_filePath);
  static QString getQuotedPath(const QString &p_path);

  ServiceLocator &m_services;
  ExportRendererFactory m_renderers;
  QHash<QString, ExportRenderer *> m_activeRenderers;
  HtmlPdfExporter *m_pdfExporter = nullptr;
  bool m_askedToStop = false;
};
} // namespace vnotex

#endif // EXPORTER_H
