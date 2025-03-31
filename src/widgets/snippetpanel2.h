#ifndef SNIPPETPANEL2_H
#define SNIPPETPANEL2_H

#include <QFrame>
#include <core/noncopyable.h>

class QListView;

namespace vnotex {
class ServiceLocator;
class TitleBar;
class SnippetListModel;
class SnippetController;

class SnippetPanel2 : public QFrame, private Noncopyable {
  Q_OBJECT

public:
  explicit SnippetPanel2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~SnippetPanel2() override = default;

  void initialize();

signals:
  void applySnippetRequested(const QString &p_name);

private slots:
  void onNewSnippetRequested();
  void onDeleteSnippetRequested(const QString &p_name);
  void onPropertiesRequested(const QString &p_name);
  void onItemActivated(const QModelIndex &p_index);
  void onContextMenuRequested(const QPoint &p_pos);

private:
  void setupUI();
  void setupTitleBar();
  void loadBuiltInVisibility();
  void saveBuiltInVisibility(bool p_visible);

  ServiceLocator &m_services;
  TitleBar *m_titleBar = nullptr;
  QListView *m_listView = nullptr;
  SnippetListModel *m_model = nullptr;
  SnippetController *m_controller = nullptr;
  bool m_builtInSnippetsVisible = true;
};

} // namespace vnotex
#endif // SNIPPETPANEL2_H
