#ifndef SYNCCONFLICTDIALOG2_H
#define SYNCCONFLICTDIALOG2_H

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class QButtonGroup;
class QPushButton;
class QRadioButton;
class QScrollArea;

namespace vnotex {

class ServiceLocator;

// SyncConflictDialog2 - Per-file conflict resolution view shown after a sync
// run reports one or more conflicts. Lists every conflicted file inside a
// scrollable area; each row exposes three mutually-exclusive radio buttons:
//
//   * Keep Local   ("keep_local",  default selection)
//   * Keep Remote  ("keep_remote")
//   * Keep Both    ("keep_both")
//
// On OK the dialog collects the chosen resolution per file into a
// QHash<QString, QString> map (file path -> one of the three string values
// above) and emits resolutionsChosen(map). On Cancel NO signal is emitted and
// no resolutions are applied; the controller is expected to leave the
// underlying conflicts unresolved (per plan ADR for T12).
//
// The dialog itself does NOT call vxcore. T13's controller drives the per-file
// vxcore_sync_resolve_conflict calls and the subsequent re-trigger.
class SyncConflictDialog2 : public QDialog {
  Q_OBJECT

public:
  explicit SyncConflictDialog2(ServiceLocator &p_services, const QString &p_notebookId,
                               const QStringList &p_conflictFiles, QWidget *p_parent = nullptr);

  // Returns the per-file resolutions currently selected in the UI. Used by
  // tests to validate selection state and by the controller to read the final
  // map after the dialog accepts.
  //
  // Map shape: { "<file path>" : "keep_local" | "keep_remote" | "keep_both" }.
  QHash<QString, QString> resolutions() const;

signals:
  // Emitted exactly once when the user clicks OK. Carries the chosen
  // resolution per conflicted file. Cancel rejects the dialog WITHOUT emitting
  // this signal.
  void resolutionsChosen(const QHash<QString, QString> &p_resolutions);

private slots:
  // Wired to QDialog::accepted(). Builds the resolution map from the row
  // button groups, emits resolutionsChosen, then accepts.
  void onAccepted();

private:
  void setupUI();

  ServiceLocator &m_services;

  QString m_notebookId;

  QStringList m_conflictFiles;

  // Per-row button groups, indexed in lock-step with m_conflictFiles. Each
  // group owns the three radio buttons (local/remote/both) for one row.
  QVector<QButtonGroup *> m_groups;

  QScrollArea *m_scrollArea = nullptr;
};

} // namespace vnotex

#endif // SYNCCONFLICTDIALOG2_H
