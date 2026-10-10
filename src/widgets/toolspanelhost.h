#ifndef TOOLSPANELHOST_H
#define TOOLSPANELHOST_H

#include <QPointer>
#include <QWidget>

class QVBoxLayout;

namespace vnotex {

class ServiceLocator;

// Borrows visual parenting only; the provider remains the panel's lifetime owner.
class ToolsPanelHost : public QWidget {
  Q_OBJECT
public:
  explicit ToolsPanelHost(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ToolsPanelHost() override;

  void setPanel(QWidget *p_owner, QWidget *p_panel);
  QWidget *getPanel() const;

signals:
  void panelChanged();

private:
  void clearBinding();
  void releasePanel();

  ServiceLocator &m_services;
  QVBoxLayout *m_layout = nullptr;
  QPointer<QWidget> m_owner;
  QPointer<QWidget> m_panel;
  QMetaObject::Connection m_ownerDestroyedConn;
  QMetaObject::Connection m_panelDestroyedConn;
};

} // namespace vnotex

#endif // TOOLSPANELHOST_H
