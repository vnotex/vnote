#ifndef VNOTE3MIGRATIONSERVICE_H
#define VNOTE3MIGRATIONSERVICE_H

#include <QDir>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace vnotex {

class NotebookCoreService;

struct LegacyTimestamp {
  qint64 value = 0;          // Milliseconds since epoch (0 if missing)
  bool parseSucceeded = true; // false if source string was present but malformed
};

struct LegacyFileEntry {
  QString name;
  QString id;
  QStringList tags;
  QString attachmentFolder;  // Numeric string like "434314329027059"
  LegacyTimestamp createdTime;
  LegacyTimestamp modifiedTime;
  QString nameColor;         // Hex color or empty
  QString backgroundColor;   // Hex color or empty
};

struct LegacyFolderEntry {
  QString name;
};

struct LegacyVxJson {
  qint64 createdTime = 0;       // Folder's own created_time (ms epoch)
  qint64 modifiedTime = 0;      // Folder's own modified_time (ms epoch)
  QString nameColor;            // Folder's own name_color
  QString backgroundColor;      // Folder's own background_color
  QList<LegacyFileEntry> files;
  QList<LegacyFolderEntry> folders;
};

struct VNote3SourceInspectionResult {
  bool valid = false;
  QString notebookName;
  QString notebookDescription;
  QStringList warnings;      // fixed ordered list for future UI
  QString imageFolder;       // Legacy image_folder value (e.g., "vx_images")
  QString attachmentFolder;  // Legacy attachment_folder value (e.g., "vx_attachments")
  QString errorMessage;
};

struct VNote3ConversionResult {
  bool success = false;
  QString destinationPath;
  QString notebookName;
  QStringList warnings; // Per-step degradation messages during conversion
  QString errorMessage;
};

class VNote3MigrationService : public QObject {
  Q_OBJECT
public:
  explicit VNote3MigrationService(NotebookCoreService *p_notebookService,
                                  QObject *p_parent = nullptr);

  VNote3SourceInspectionResult inspectSourceNotebook(const QString &p_sourcePath) const;
  VNote3ConversionResult convertNotebook(const QString &p_sourcePath, const QString &p_destPath);

  static LegacyVxJson parseLegacyVxJson(const QString &p_vxJsonPath);
  static QStringList collectAllTags(const QString &p_sourcePath);

private:
  void importFolder(const QString &p_notebookId, const QDir &p_sourceRoot,
                    const QString &p_relFolderPath, const QString &p_destPath,
                    const VNote3SourceInspectionResult &p_inspection, QStringList &p_warnings);

  NotebookCoreService *m_notebookService = nullptr;
};

} // namespace vnotex

#endif // VNOTE3MIGRATIONSERVICE_H
