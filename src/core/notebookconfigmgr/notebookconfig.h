#ifndef NOTEBOOKCONFIG_H
#define NOTEBOOKCONFIG_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSharedPointer>
#include <QVector>

#include "bundlenotebookconfigmgr.h"
#include <core/global.h>
#include <core/historyitem.h>

namespace vnotex {
class NotebookParameters;

// Notebook config of BundleNotebook.
class NotebookConfig {
public:
  virtual ~NotebookConfig() {}

  static QSharedPointer<NotebookConfig> fromNotebookParameters(int p_version,
                                                               const NotebookParameters &p_paras);

  static QSharedPointer<NotebookConfig> fromNotebook(int p_version,
                                                     const BundleNotebook *p_notebook);

  virtual QJsonObject toJson() const;

  virtual void fromJson(const QJsonObject &p_jobj);

  int m_version = 0;

  QString m_name;

  QString m_description;

  QString m_imageFolder;

  QString m_attachmentFolder;

  QDateTime m_createdTimeUtc;

  QString m_versionController;

  QString m_notebookConfigMgr;

  QVector<HistoryItem> m_history;

  // Graph of tags of this notebook like "parent>chlid;parent2>chlid2".
  QString m_tagGraph;

  // Hold all the extra configs for other components or 3rd party plugins.
  // Use a unique name as the key and the value is a QJsonObject.
  QJsonObject m_extraConfigs;

private:
  QJsonArray saveHistory() const;

  void loadHistory(const QJsonObject &p_jobj);
};
} // namespace vnotex

#endif // NOTEBOOKCONFIG_H
