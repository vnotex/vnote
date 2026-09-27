#ifndef NOTEBOOKSYNCINFODIALOG2_H
#define NOTEBOOKSYNCINFODIALOG2_H

#include <QString>
#include <QStringList>

#include "scrolldialog.h"
#include <core/services/syncsettings.h>
#include <vxcore/vxcore_types.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace vnotex {

class InlineBanner;
class NotebookSyncInfoController;
class ServiceLocator;

// View for existing or pre-create sync settings. Secrets are never prefilled;
// the controller retrieves retained credentials only for an apply operation.
class NotebookSyncInfoDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  explicit NotebookSyncInfoDialog2(ServiceLocator &p_services, const QString &p_notebookId,
                                   QWidget *p_parent = nullptr);
  explicit NotebookSyncInfoDialog2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  void setPreCreateNotebookName(const QString &p_name);
  void setBootstrapMode(bool p_enabled);
  void setBackend(const QString &p_backend);
  SyncSettings enteredSettings() const;
  QString enteredRemoteUrl() const;
  bool isPreCreateMode() const;
  bool changesPending() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;
  void appliedButtonClicked() Q_DECL_OVERRIDE;
  void resetButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void onFieldEdited();
  void onDisableSyncClicked();
  void onSyncStarted(const QString &p_notebookId);
  void onSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onSyncFailed(const QString &p_notebookId, VxCoreError p_code, const QString &p_message);
  void onConflictsDetected(const QString &p_notebookId, const QStringList &p_conflictFiles);
  void onConfirmUrlChange(const QString &p_oldUrl, const QString &p_newUrl);
  void onError(const QString &p_message);

private:
  enum class SyncStateLevel { Idle, Syncing, Conflict, Error };

  void setupUI();
  void connectSyncServiceSignals();
  void setCurrentStateLabel(SyncStateLevel p_level, const QString &p_text);
  void refreshDirtyButtons();
  void refreshBackendFields();
  void refreshReadOnlyBanner();
  void applySettings(bool p_closeOnSuccess);

  ServiceLocator &m_services;
  QString m_notebookId;
  NotebookSyncInfoController *m_controller = nullptr;

  QLabel *m_notebookNameLabel = nullptr;
  InlineBanner *m_readOnlyBanner = nullptr;
  QComboBox *m_backendCombo = nullptr;
  QLabel *m_remoteUrlLabel = nullptr;
  QLineEdit *m_remoteUrlEdit = nullptr;
  QLabel *m_remoteUrlHintLabel = nullptr;
  InlineBanner *m_webdavHint = nullptr;
  QLabel *m_gitUsernameLabel = nullptr;
  QLineEdit *m_gitUsernameEdit = nullptr;
  QLabel *m_webdavUsernameLabel = nullptr;
  QLineEdit *m_webdavUsernameEdit = nullptr;
  QLabel *m_secretLabel = nullptr;
  QLineEdit *m_patEdit = nullptr;
  QLabel *m_lastSyncLabel = nullptr;
  QLabel *m_currentStateLabel = nullptr;
  QPushButton *m_disableSyncButton = nullptr;

  QString m_lastAppliedRemoteUrl;
  QString m_lastAppliedBackend = QStringLiteral("git");
  bool m_bootstrapMode = false;
  bool m_preCreateMode = false;
  bool m_isReadOnlyNotebook = false;
  bool m_syncEnabled = false;
  bool m_rawNotebook = false;
  bool m_applying = false;
};

} // namespace vnotex

#endif // NOTEBOOKSYNCINFODIALOG2_H
