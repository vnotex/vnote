#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>
#include <QString>

#include <core/services/updateservice.h>

class QLabel;
class QPushButton;
class QTextBrowser;

namespace vnotex {

// Presents the outcome of an update check.
//
// Pure view: it renders the UpdateInfo it is given and emits intents. VNote
// downloads nothing, so the only affordance for an available update is the
// release page. Every decision (whether a version is skipped, whether to
// throttle) belongs to UpdateController.
class UpdateDialog : public QDialog {
  Q_OBJECT

public:
  UpdateDialog(const UpdateInfo &p_info, QWidget *p_parent = nullptr);

  void setFailed(const QString &p_message);

signals:
  // "Skip this version": never offer this exact version again.
  void skipRequested(const QString &p_version);

private:
  void setupUi();

  const UpdateInfo m_info;

  QLabel *m_headline = nullptr;
  QLabel *m_summary = nullptr;
  QTextBrowser *m_notes = nullptr;
  QLabel *m_status = nullptr;

  QPushButton *m_releasePageButton = nullptr;
  QPushButton *m_skipButton = nullptr;
  QPushButton *m_laterButton = nullptr;
};

} // namespace vnotex

#endif // UPDATEDIALOG_H
