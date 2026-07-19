#ifndef UNITEDENTRY_H
#define UNITEDENTRY_H

#include <QSharedPointer>
#include <QSize>
#include <QWidget>

class QAction;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QComboBox;
class QKeyEvent;
class QToolButton;
class QHBoxLayout;

namespace vnotex {
class ServiceLocator;
class EntryPopup;
class IUnitedEntry;
class UnitedEntryMgr;
class UnitedEntryHelper;

class UnitedEntry : public QWidget {
  Q_OBJECT
public:
  explicit UnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                       const QSize &p_iconSize, QWidget *p_parent = nullptr);

  ~UnitedEntry();

  bool eventFilter(QObject *p_watched, QEvent *p_event) Q_DECL_OVERRIDE;

  QAction *getActivateAction();

  void activate();

  void deactivate();

private:
  void setupUI();

  void setupActions();

  void clear();

  void ensureActivated(QWidget *p_previousFocus);

  void processInput();

  void handleFocusChanged(QWidget *p_old, QWidget *p_now);

  void handleAppStateChanged(Qt::ApplicationState p_state);

  void scheduleRestorePopup();

  void restorePopupVisibility();

  const QSharedPointer<QTreeWidget> &getEntryListWidget();

  QSharedPointer<QLabel> getInfoWidget(const QString &p_info);

  void popupWidget(const QSharedPointer<QWidget> &p_widget);

  // Return true if there is any entry visible.
  bool filterEntryListWidgetEntries(const QString &p_name);

  void handleEntryFinished(IUnitedEntry *p_entry);

  void handleEntryListItemActivated(QTreeWidgetItem *p_item);

  void handleEntryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus);

  void setBusy(bool p_busy);

  void exitUnitedEntry();

  // Return true if want to stop the propogation.
  bool handleLineEditKeyPress(QKeyEvent *p_event);

  bool isUnitedEntryShortcut(QKeyEvent *p_event) const;

  QSize calculatePopupSize() const;

  void updatePopupGeometry();

  void refreshIcons();

  void populateHistoryItems();

  ServiceLocator &m_services;

  UnitedEntryMgr *m_mgr = nullptr;

  QComboBox *m_comboBox = nullptr;

  QToolButton *m_toggleButton = nullptr;

  QSize m_iconSize;

  QAction *m_activateAction = nullptr;

  EntryPopup *m_popup = nullptr;

  QAction *m_menuIconAction = nullptr;

  QAction *m_busyIconAction = nullptr;

  bool m_activated = false;

  bool m_deactivatePending = false;

  bool m_restorePending = false;

  // Set only when the app is deactivated while activated (the OS natively hides
  // the Qt::Tool popup). Gates the re-map restore so repeated clicks while the
  // app stays active do not needlessly hide/show the already-visible popup.
  bool m_popupNeedsRestore = false;

  QWidget *m_previousFocusWidget = nullptr;

  QTimer *m_processTimer = nullptr;

  QSharedPointer<QTreeWidget> m_entryListWidget = nullptr;

  QSharedPointer<IUnitedEntry> m_lastEntry;

  bool m_hasPending = false;
};
} // namespace vnotex

#endif // UNITEDENTRY_H
