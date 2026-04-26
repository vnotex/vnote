#ifndef OUTLINEVIEWER_H
#define OUTLINEVIEWER_H

#include <QFrame>
#include <QScopedPointer>
#include <QSharedPointer>

namespace vnotex {

class NavigationMode;
class OutlineController;
class OutlineProvider;
class OutlineView;
class ServiceLocator;
class TitleBar;
class TreeFilterProxyModel;
template <typename T> class NavigationModeViewWrapper;

class OutlineViewer : public QFrame {
  Q_OBJECT
public:
  OutlineViewer(ServiceLocator &p_services, const QString &p_title,
                QWidget *p_parent = nullptr);
  ~OutlineViewer() override;

  void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

  NavigationMode *getOutlineNavigationWrapper() const;

signals:
  void focusViewArea();

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI(const QString &p_title);

  TitleBar *setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

  void showLevel();

  ServiceLocator &m_services;

  // Owned (child QObject).
  OutlineController *m_controller = nullptr;

  // Owned by layout (child widget).
  OutlineView *m_outlineView = nullptr;

  // Proxy model for search filtering (child QObject).
  TreeFilterProxyModel *m_proxyModel = nullptr;

  QScopedPointer<NavigationModeViewWrapper<OutlineView>> m_outlineNavWrapper;
};

} // namespace vnotex

#endif // OUTLINEVIEWER_H
