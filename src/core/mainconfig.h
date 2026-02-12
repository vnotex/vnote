#ifndef MAINCONFIG_H
#define MAINCONFIG_H

#include "iconfig.h"

#include <QString>
#include <QtGlobal>
#include <QVersionNumber>

class QJsonObject;

namespace vnotex {
class CoreConfig;
class EditorConfig;
class WidgetConfig;
class IConfigMgr;

class MainConfig : public IConfig {
public:
  explicit MainConfig(IConfigMgr *p_mgr);

  ~MainConfig();

  void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

  const QString &getVersion() const { return m_version; }
  void setVersion(const QString &p_version) { m_version = p_version; }

  CoreConfig &getCoreConfig();

  EditorConfig &getEditorConfig();

  WidgetConfig &getWidgetConfig();

  void update() Q_DECL_OVERRIDE;

  QJsonObject toJson() const Q_DECL_OVERRIDE;

  static QString peekVersion(const QJsonObject &p_jboj);

private:
  void loadMetadata(const QJsonObject &p_jobj);

  QJsonObject saveMetaData() const;

  void doVersionSpecificOverride();

  QString m_version;

  enum ChildConfigIndex {
    CoreConfigIndex = 0,
    EditorConfigIndex,
    WidgetConfigIndex,
    ChildConfigCount
  };

  QVector<QSharedPointer<IConfig>> m_childConfigs;
};
} // namespace vnotex

#endif // MAINCONFIG_H
