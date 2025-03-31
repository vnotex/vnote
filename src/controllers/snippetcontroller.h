#ifndef SNIPPETCONTROLLER_H
#define SNIPPETCONTROLLER_H

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;
class SnippetCoreService;

class SnippetController : public QObject {
  Q_OBJECT

public:
  explicit SnippetController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Business logic.
  bool createSnippet(const QString &p_name, const QJsonObject &p_contentJson);
  bool deleteSnippet(const QString &p_name);
  bool renameSnippet(const QString &p_oldName, const QString &p_newName);
  bool updateSnippet(const QString &p_name, const QJsonObject &p_contentJson);
  QJsonObject getSnippet(const QString &p_name) const;
  QString getSnippetFolderPath() const;

  // Shortcut management (VNote-side).
  void setShortcut(const QString &p_name, int p_shortcut);
  void removeShortcut(const QString &p_name);
  int getShortcutForSnippet(const QString &p_name) const;
  QList<int> getAvailableShortcuts() const;
  QMap<QString, int> getAllShortcuts() const;

public slots:
  void requestApplySnippet(const QString &p_name);
  void requestNewSnippet();
  void requestDeleteSnippet(const QString &p_name);
  void requestProperties(const QString &p_name);

signals:
  void applySnippetRequested(const QString &p_name);
  void newSnippetRequested();
  void deleteSnippetRequested(const QString &p_name);
  void propertiesRequested(const QString &p_name);
  void snippetListChanged();
  void errorOccurred(const QString &p_message);

private:
  ServiceLocator &m_services;
  SnippetCoreService *m_snippetService = nullptr;
  QMap<QString, int> m_nameToShortcut;
  QMap<int, QString> m_shortcutToName;
};

} // namespace vnotex

#endif // SNIPPETCONTROLLER_H
