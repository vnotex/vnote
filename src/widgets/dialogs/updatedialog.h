#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>
#include <QString>

#include <core/services/updateservice.h>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

namespace vnotex {

// Presents the outcome of an update check and drives the download.
//
// Pure view: it renders the UpdateInfo it is given and emits intents. Every
// decision (whether a version is skipped, whether to throttle, what to do on
// quit) belongs to UpdateController.
class UpdateDialog : public QDialog {
  Q_OBJECT

public:
  UpdateDialog(const UpdateInfo &p_info, QWidget *p_parent = nullptr);

  // Switches the dialog into its downloading state: buttons disabled, progress
  // bar shown.
  void setDownloading();

  void setProgress(const QString &p_stage, qint64 p_done, qint64 p_total);

  // Terminal state: staging succeeded and the update will be applied on quit.
  void setReadyToApply(const QString &p_version);

  void setFailed(const QString &p_message);

signals:
  // "Update": download and stage now.
  void downloadRequested();

  // "Skip this version": never offer this exact version again.
  void skipRequested(const QString &p_version);

  // "Restart now": apply the staged update immediately.
  void restartRequested();

private:
  void setupUi();

  static QString formatBytes(qint64 p_bytes);

  const UpdateInfo m_info;

  QLabel *m_headline = nullptr;
  QLabel *m_summary = nullptr;
  QTextBrowser *m_notes = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_status = nullptr;

  QPushButton *m_updateButton = nullptr;
  QPushButton *m_skipButton = nullptr;
  QPushButton *m_laterButton = nullptr;
  QPushButton *m_restartButton = nullptr;
};

} // namespace vnotex

#endif // UPDATEDIALOG_H
