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

  // Force-overwrite specific config values when upgrading FROM @p_previousVersion,
  // bypassing the usual "merged user config wins" behavior. Each override is gated
  // on the version it was introduced in, so it fires only for the relevant upgrade
  // (never on downgrade, unrelated future upgrades, or forced-debug refreshes where
  // previous == current). Schedules a persistence write via the parent update chain.
  void doVersionSpecificOverride(const QString &p_previousVersion);

private:
  void loadMetadata(const QJsonObject &p_jobj);

  QJsonObject saveMetaData() const;

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
