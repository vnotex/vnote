#include "templateservice.h"

#include <QDebug>
#include <QDir>

#include <core/configmgr2.h>
#include <utils/fileutils2.h>

using namespace vnotex;

TemplateService::TemplateService(ConfigMgr2 *p_configMgr, QObject *p_parent)
    : QObject(p_parent), m_configMgr(p_configMgr) {
  Q_ASSERT(m_configMgr != nullptr);
}

QString TemplateService::getTemplateFolder() const {
  return m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Templates);
}

bool TemplateService::ensureTemplateFolder() const {
  const QString folderPath = getTemplateFolder();
  QDir dir(folderPath);
  if (dir.exists()) {
    return true;
  }
  return dir.mkpath(folderPath);
}

QStringList TemplateService::getTemplates() const {
  QDir dir(getTemplateFolder());
  dir.setFilter(QDir::Files | QDir::NoSymLinks);
  QStringList names = dir.entryList();
  // The extra-data version stamp lives in this folder since templates/ became a
  // bundled folder. It is not hidden on Windows (no dot-file convention there),
  // so it must be filtered out explicitly or it shows up as a template.
  names.removeAll(QLatin1String(FileUtils2::c_versionStampFileName));
  return names;
}

QString TemplateService::getTemplateFilePath(const QString &p_name) const {
  if (p_name.isEmpty()) {
    return QString();
  }
  return QDir(getTemplateFolder()).filePath(p_name);
}

QString TemplateService::getTemplateContent(const QString &p_name) const {
  const auto filePath = getTemplateFilePath(p_name);
  if (filePath.isEmpty()) {
    return QString();
  }
  QString text;
  Error err = FileUtils2::readTextFile(filePath, &text);
  if (err) {
    qWarning() << err.what();
    return QString();
  }
  return text;
}
