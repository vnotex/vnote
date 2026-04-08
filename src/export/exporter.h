#ifndef EXPORTER_H
#define EXPORTER_H

#include <QObject>
#include <QStringList>
#include <QVector>

#include "exportdata.h"
#include <core/servicelocator.h>

namespace vnotex {
class WebViewExporter;

struct ExportFileInfo {
  QString filePath;
  QString fileName;
  QString resourcePath;
  QString attachmentFolderPath;
  bool isMarkdown = true;
};

class Exporter : public QObject {
  Q_OBJECT
public:
  // We need the QWidget as parent.
  explicit Exporter(ServiceLocator &p_services, QWidget *p_parent);

  // Return exported output file.
  QString doExportFile(const ExportOption &p_option, const QString &p_content,
                       const QString &p_filePath, const QString &p_fileName,
                       const QString &p_resourcePath, const QString &p_attachmentFolderPath,
                       bool p_isMarkdown);

  // Return exported output files.
  QStringList doExportBatch(const ExportOption &p_option, const QVector<ExportFileInfo> &p_files,
                            const QString &p_batchName);

  void stop();

signals:
  void progressUpdated(int p_val, int p_maximum);

  void logRequested(const QString &p_log);

  void askedToStop();

private:
  QString doExport(const ExportOption &p_option, const QString &p_outputDir,
                   const ExportFileInfo &p_file, const QString &p_content = QString());

  QString doExportMarkdown(const ExportOption &p_option, const QString &p_outputDir,
                           const QString &p_content, const QString &p_filePath,
                           const QString &p_fileName, const QString &p_resourcePath,
                           const QString &p_attachmentFolderPath);

  QString doExportHtml(const ExportOption &p_option, const QString &p_outputDir,
                       const QString &p_content, const QString &p_filePath,
                       const QString &p_fileName, const QString &p_resourcePath,
                       const QString &p_destPath, const QString &p_attachmentFolderPath);

  QString doExportPdf(const ExportOption &p_option, const QString &p_outputDir,
                      const QString &p_content, const QString &p_filePath,
                      const QString &p_fileName, const QString &p_resourcePath,
                      const QString &p_destPath, const QString &p_attachmentFolderPath);

  QString doExportCustom(const ExportOption &p_option, const QString &p_outputDir,
                         const QString &p_content, const QString &p_filePath,
                         const QString &p_fileName, const QString &p_resourcePath,
                         const QString &p_destPath, const QString &p_attachmentFolderPath);

  bool doExportCustom(const ExportOption &p_option, const QStringList &p_files,
                      const QStringList &p_resourcePaths, const QString &p_filePath);

  QString doExportPdfAllInOne(const ExportOption &p_option, const QVector<ExportFileInfo> &p_files,
                              const QString &p_batchName);

  QString doExportCustomAllInOne(const ExportOption &p_option,
                                 const QVector<ExportFileInfo> &p_files,
                                 const QString &p_batchName);

  void exportAttachments(const QString &p_attachmentFolderPath, const QString &p_srcFilePath,
                         const QString &p_outputFolder, const QString &p_destFilePath);

  WebViewExporter *getWebViewExporter(const ExportOption &p_option);

  void cleanUpWebViewExporter();

  void cleanUp();

  bool checkAskedToStop() const;

  static ExportOption getExportOptionForIntermediateHtml(const ExportOption &p_option,
                                                         const QString &p_outputDir);

  static QString evaluateCommand(const ExportOption &p_option, const QStringList &p_files,
                                 const QStringList &p_resourcePaths, const QString &p_filePath);

  static QString getQuotedPath(const QString &p_path);

  ServiceLocator &m_services;

  // Managed by QObject.
  WebViewExporter *m_webViewExporter = nullptr;

  bool m_askedToStop = false;
};
} // namespace vnotex

#endif // EXPORTER_H
