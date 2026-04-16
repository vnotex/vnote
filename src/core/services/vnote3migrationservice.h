#ifndef VNOTE3MIGRATIONSERVICE_H
#define VNOTE3MIGRATIONSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

namespace vnotex {

class NotebookCoreService;

struct VNote3SourceInspectionResult {
  bool valid = false;
  QString notebookName;
  QString notebookDescription;
  QStringList warnings; // fixed ordered list for future UI
  QString errorMessage;
};

struct VNote3ConversionResult {
  bool success = false;
  QString destinationPath;
  QString notebookName;
  QString errorMessage;
};

class VNote3MigrationService : public QObject {
  Q_OBJECT
public:
  explicit VNote3MigrationService(NotebookCoreService *p_notebookService,
                                  QObject *p_parent = nullptr);

  VNote3SourceInspectionResult inspectSourceNotebook(const QString &p_sourcePath) const;
  VNote3ConversionResult convertNotebook(const QString &p_sourcePath, const QString &p_destPath);

private:
  NotebookCoreService *m_notebookService = nullptr;
};

} // namespace vnotex

#endif // VNOTE3MIGRATIONSERVICE_H
