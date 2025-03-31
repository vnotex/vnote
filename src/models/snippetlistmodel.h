#ifndef SNIPPETLISTMODEL_H
#define SNIPPETLISTMODEL_H

#include <QAbstractListModel>
#include <QMap>
#include <QString>
#include <QVector>

namespace vnotex {

class SnippetCoreService;

class SnippetListModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    DescriptionRole,
    TypeRole,
    IsBuiltinRole,
    ShortcutRole
  };

  explicit SnippetListModel(SnippetCoreService *p_snippetService, QObject *p_parent = nullptr);
  ~SnippetListModel() override = default;

  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;

  void refresh();
  void setShowBuiltIn(bool p_show);
  QString snippetAt(int p_row) const;
  void setShortcuts(const QMap<QString, int> &p_shortcuts);

private:
  struct SnippetInfo {
    QString name;
    QString description;
    QString type;
    bool isBuiltin = false;
    int shortcut = -1;
  };

  SnippetCoreService *m_snippetService = nullptr;
  QVector<SnippetInfo> m_allSnippets;
  QVector<SnippetInfo> m_displaySnippets;
  QMap<QString, int> m_shortcuts;
  bool m_showBuiltIn = true;

  void applyFilter();
};

} // namespace vnotex

#endif // SNIPPETLISTMODEL_H
