#ifndef OUTLINEVIEWER_H
#define OUTLINEVIEWER_H

#include <QFrame>
#include <QSharedPointer>

namespace vnotex {

class OutlineController;
class OutlineProvider;
class OutlineView;
class ServiceLocator;
class TitleBar;

class OutlineViewer : public QFrame {
  Q_OBJECT
public:
  OutlineViewer(ServiceLocator &p_services, const QString &p_title,
                QWidget *p_parent = nullptr);

  void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

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
};

} // namespace vnotex

#endif // OUTLINEVIEWER_H
