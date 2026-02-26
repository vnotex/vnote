#ifndef TEMPLATESERVICE_H
#define TEMPLATESERVICE_H

#include <QObject>
#include <QStringList>

#include "core/noncopyable.h"

namespace vnotex {

class ConfigMgr2;

// Service for managing note templates.
// Provides template listing, file path resolution, and content reading.
// Receives ConfigMgr2 via constructor for dependency injection.
class TemplateService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives ConfigMgr2 via DI.
  // The ConfigMgr2 must remain valid for the lifetime of this service.
  explicit TemplateService(ConfigMgr2 *p_configMgr, QObject *p_parent = nullptr);

  // Get the folder path containing templates.
  QString getTemplateFolder() const;

  // Ensure the template folder exists, creating it if necessary.
  // Returns true if folder exists or was created successfully.
  bool ensureTemplateFolder() const;

  // Get list of template file names.
  QStringList getTemplates() const;

  // Get full file path for a template by name.
  // Returns empty string if p_name is empty.
  QString getTemplateFilePath(const QString &p_name) const;

  // Get content of a template file by name.
  // Returns empty string if p_name is empty or file cannot be read.
  QString getTemplateContent(const QString &p_name) const;

private:
  ConfigMgr2 *m_configMgr = nullptr;
};

} // namespace vnotex

#endif // TEMPLATESERVICE_H
