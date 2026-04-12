#ifndef UNITEDENTRY_H
#define UNITEDENTRY_H

#include <QSharedPointer>
#include <QWidget>

class QAction;
class QTimer;
class QTreeWidget;
class QLabel;
class QComboBox;
class QKeyEvent;

namespace vnotex {
class ServiceLocator;
class EntryPopup;
class IUnitedEntry;
class UnitedEntryMgr;

class UnitedEntry : public QWidget {
  Q_OBJECT
public:
  explicit UnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                       QWidget *p_parent = nullptr);

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

  const QSharedPointer<QTreeWidget> &getEntryListWidget();

  QSharedPointer<QLabel> getInfoWidget(const QString &p_info);

  void popupWidget(const QSharedPointer<QWidget> &p_widget);

  // Return true if there is any entry visible.
  bool filterEntryListWidgetEntries(const QString &p_name);

  void handleEntryFinished(IUnitedEntry *p_entry);

  void handleEntryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus);

  void setBusy(bool p_busy);

  void exitUnitedEntry();

  // Return true if want to stop the propogation.
  bool handleLineEditKeyPress(QKeyEvent *p_event);

  bool isUnitedEntryShortcut(QKeyEvent *p_event) const;

  QSize calculatePopupSize() const;

  void updatePopupGeometry();

  ServiceLocator &m_services;

  UnitedEntryMgr *m_mgr = nullptr;

  QComboBox *m_comboBox = nullptr;

  EntryPopup *m_popup = nullptr;

  QAction *m_menuIconAction = nullptr;

  QAction *m_busyIconAction = nullptr;

  bool m_activated = false;

  QWidget *m_previousFocusWidget = nullptr;

  QTimer *m_processTimer = nullptr;

  QSharedPointer<QTreeWidget> m_entryListWidget = nullptr;

  QSharedPointer<IUnitedEntry> m_lastEntry;

  bool m_hasPending = false;
};
} // namespace vnotex

#endif // UNITEDENTRY_H
