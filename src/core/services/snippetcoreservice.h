#ifndef SNIPPETCORESERVICE_H
#define SNIPPETCORESERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

class SnippetCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit SnippetCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~SnippetCoreService();

  QString getSnippetFolderPath() const;
  QJsonArray listSnippets() const;
  QJsonObject getSnippet(const QString &p_name) const;
  bool createSnippet(const QString &p_name, const QJsonObject &p_contentJson);
  bool deleteSnippet(const QString &p_name);
  bool renameSnippet(const QString &p_oldName, const QString &p_newName);
  bool updateSnippet(const QString &p_name, const QJsonObject &p_contentJson);
  QJsonObject applySnippet(const QString &p_name, const QString &p_selectedText,
                           const QString &p_indentation,
                           const QJsonObject &p_overrides) const;
  // Resolve %snippet_name% symbols in arbitrary text.
  // For each match, looks up snippet by name via getSnippet() and applySnippet(),
  // then replaces the %name% with the expanded text.
  QString applySnippetBySymbol(const QString &p_content) const;

private:
  bool checkContext() const;
  static QString cstrToQString(char *p_str);
  static QJsonObject parseJsonObjectFromCStr(char *p_str);
  static QJsonArray parseJsonArrayFromCStr(char *p_str);

  VxCoreContextHandle m_context = nullptr;
};

} // namespace vnotex

#endif // SNIPPETCORESERVICE_H
