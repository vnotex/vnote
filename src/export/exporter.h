#ifndef EXPORTER_H
#define EXPORTER_H

#include <QObject>
#include <QStringList>

#include "exportdata.h"

namespace vnotex {
class Notebook;
class Node;
class Buffer;
class File;
class WebViewExporter;

class Exporter : public QObject {
  Q_OBJECT
public:
  // We need the QWidget as parent.
  explicit Exporter(QWidget *p_parent);

  // Return exported output file.
  QString doExport(const ExportOption &p_option, Buffer *p_buffer);

  // Return exported output file.
  QString doExport(const ExportOption &p_option, Node *p_note);

  // Return exported output files.
  QStringList doExportFolder(const ExportOption &p_option, Node *p_folder);

  QStringList doExport(const ExportOption &p_option, Notebook *p_notebook);

  void stop();

signals:
  void progressUpdated(int p_val, int p_maximum);

  void logRequested(const QString &p_log);

private:
  QStringList doExport(const ExportOption &p_option, const QString &p_outputDir, Node *p_folder);

  QString doExport(const ExportOption &p_option, const QString &p_outputDir, const File *p_file);

  QString doExportMarkdown(const ExportOption &p_option, const QString &p_outputDir,
                           const File *p_file);

  QString doExportHtml(const ExportOption &p_option, const QString &p_outputDir,
                       const File *p_file);

  QString doExportPdf(const ExportOption &p_option, const QString &p_outputDir, const File *p_file);

  QString doExportCustom(const ExportOption &p_option, const QString &p_outputDir,
                         const File *p_file);

  bool doExportCustom(const ExportOption &p_option, const QStringList &p_files,
                      const QStringList &p_resourcePaths, const QString &p_filePath);

  // Export @p_notebook or @p_folder. @p_folder will be considered only when @p_notebook is null.
  QString doExportPdfAllInOne(const ExportOption &p_option, Notebook *p_notebook, Node *p_folder);

  QString doExportCustomAllInOne(const ExportOption &p_option, Notebook *p_notebook,
                                 Node *p_folder);

  void exportAttachments(Node *p_node, const QString &p_srcFilePath, const QString &p_outputFolder,
                         const QString &p_destFilePath);

  WebViewExporter *getWebViewExporter(const ExportOption &p_option);

  void cleanUpWebViewExporter();

  void cleanUp();

  bool checkAskedToStop() const;

  QStringList doExportNotebook(const ExportOption &p_option, const QString &p_outputDir,
                               Notebook *p_notebook);

  static ExportOption getExportOptionForIntermediateHtml(const ExportOption &p_option,
                                                         const QString &p_outputDir);

  static QString evaluateCommand(const ExportOption &p_option, const QStringList &p_files,
                                 const QStringList &p_resourcePaths, const QString &p_filePath);

  static QString getQuotedPath(const QString &p_path);

  static void collectFiles(const QList<QSharedPointer<File>> &p_files, QStringList &p_inputFiles,
                           QStringList &p_resourcePaths);

  // Managed by QObject.
  WebViewExporter *m_webViewExporter = nullptr;

  bool m_askedToStop = false;
};
} // namespace vnotex

#endif // EXPORTER_H
