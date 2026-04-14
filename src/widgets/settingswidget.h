#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <functional>

#include <core/iviewwindowcontent.h>

class QLineEdit;
class QBoxLayout;
class QScrollArea;
class QStackedLayout;
class QTimer;
class QToolBar;
class QTreeWidgetItem;
class QAction;

namespace vnotex {
class MainWindow2;
class ServiceLocator;
class SettingsPage;
class TreeWidget;

class SettingsWidget : public QWidget, public IViewWindowContent {
  Q_OBJECT
public:
  SettingsWidget(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                 QWidget *p_parent = nullptr);

  QString title() const override;
  QIcon icon() const override;
  QString virtualAddress() const override;
  void setupToolBar(QToolBar *p_toolBar) override;
  bool isDirty() const override;
  bool save() override;
  void reset() override;
  bool canClose(bool p_force) override;
  QWidget *contentWidget() override;

signals:
  void contentChanged();

private:
  void setupUI();
  void setupPageExplorer(QBoxLayout *p_layout, QWidget *p_parent);
  void setupPages();
  void setupPage(QTreeWidgetItem *p_item, SettingsPage *p_page);
  SettingsPage *itemPage(QTreeWidgetItem *p_item) const;
  void setChangesUnsaved(bool p_unsaved);
  bool savePages();
  void resetPages();
  void forEachPage(const std::function<bool(SettingsPage *)> &p_func);
  QTreeWidgetItem *addPage(SettingsPage *p_page);
  QTreeWidgetItem *addSubPage(SettingsPage *p_page, QTreeWidgetItem *p_parentItem);
  void search();
  void checkRestart();

  ServiceLocator &m_services;
  MainWindow2 *m_mainWindow = nullptr;
  QLineEdit *m_searchEdit = nullptr;
  TreeWidget *m_pageExplorer = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QStackedLayout *m_pageLayout = nullptr;
  bool m_changesUnsaved = false;
  bool m_ready = false;
  QTimer *m_searchTimer = nullptr;
  QAction *m_applyAction = nullptr;
  QAction *m_resetAction = nullptr;
};
} // namespace vnotex

#endif // SETTINGSWIDGET_H
