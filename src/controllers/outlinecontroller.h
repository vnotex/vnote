#ifndef OUTLINECONTROLLER_H
#define OUTLINECONTROLLER_H

#include <QObject>
#include <QSharedPointer>

class QTimer;

namespace vnotex {

class OutlineModel;
class OutlineView;
class OutlineProvider;
class ServiceLocator;

// Controller that orchestrates the OutlineModel and OutlineView for the
// outline (table of contents) panel. Bridges between OutlineProvider (the
// data source from the active ViewWindow) and the MVC components. Handles
// config reads/writes for expand level and section number settings.
//
// This is a QObject, NOT a QWidget. It is testable and does not own the view.
class OutlineController : public QObject {
  Q_OBJECT

public:
  explicit OutlineController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~OutlineController() override;

  // Set the view this controller manages. Wires view signals.
  // Does NOT take ownership (view is parented to a widget).
  void setView(OutlineView *p_view);
  OutlineView *view() const;

  // Get the model (for the view to use).
  OutlineModel *model() const;

  // Set the active outline provider (from the current ViewWindow).
  // Disconnects from previous provider, connects to new one, updates model.
  // Pass nullptr when no view window is active.
  void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

  // Expand level controls (1 = collapse all, 6 = expand all).
  void increaseExpandLevel();
  void decreaseExpandLevel();
  int getExpandLevel() const;

  // Section number toggle.
  void toggleSectionNumber();
  bool isSectionNumberEnabled() const;

signals:
  // Emitted when expand level changes (for UI to show tooltip).
  void expandLevelChanged(int p_level);

  // Emitted to request the view area to focus (when user clicks a heading).
  void focusViewAreaRequested();

private:
  // Get the base heading level (level of the first heading in the current
  // outline). Returns 1 if no outline or empty.
  int getBaseLevel() const;

  // Apply current expand level to the view.
  void applyExpandLevel();

  // Update the model with the current provider's outline data.
  void updateModelFromProvider();

  ServiceLocator &m_services;
  OutlineModel *m_model = nullptr;       // Owned (child QObject)
  OutlineView *m_view = nullptr;         // Not owned
  QSharedPointer<OutlineProvider> m_provider;
  QTimer *m_expandTimer = nullptr;       // Debounce timer for auto-expand
  int m_autoExpandedLevel = 6;           // Cached from config
  bool m_sectionNumberEnabled = false;   // Cached from config
};

} // namespace vnotex

#endif // OUTLINECONTROLLER_H
