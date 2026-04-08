#ifndef EXPORTCONTROLLER_H
#define EXPORTCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <core/exportcontext.h>
#include <export/exportdata.h>

class QWidget;

namespace vnotex {

class Exporter;
struct ExportFileInfo;
class ServiceLocator;

class ExportController : public QObject {
  Q_OBJECT

public:
  explicit ExportController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ExportController(ServiceLocator &p_services, QWidget *p_widgetParent,
                   QObject *p_parentObject = nullptr);

  void doExport(const ExportOption &p_option, const ExportContext &p_context);
  void stop();
  bool isExporting() const;

signals:
  void exportFinished(const QStringList &p_outputFiles);
  void progressUpdated(int p_val, int p_maximum);
  void logRequested(const QString &p_log);

private:
  Exporter *ensureExporter();
  void collectExportFiles(const QString &p_notebookId, const QString &p_folderPath,
                          bool p_recursive, bool p_exportAttachments,
                          QVector<ExportFileInfo> &p_files);
  bool isMarkdownFile(const QString &p_filePath) const;
  QString normalizedRelativePath(const QString &p_relativePath) const;
  QString notebookBatchName(const QString &p_notebookId) const;
  QString folderBatchName(const QString &p_notebookId, const QString &p_folderPath) const;

  ServiceLocator &m_services;
  QPointer<QWidget> m_widgetParent;
  QPointer<Exporter> m_exporter;
  bool m_isExporting = false;
};

} // namespace vnotex

#endif // EXPORTCONTROLLER_H
