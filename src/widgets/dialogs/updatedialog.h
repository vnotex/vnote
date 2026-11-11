#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>
#include <QString>

#include <core/services/updateservice.h>

class QLabel;
class QPushButton;

namespace vnotex {

class ServiceLocator;

// Presents the outcome of an update check.
//
// Pure view: renders UpdateInfo and emits intents. The release page is always
// available for a newer version; Windows also offers an explicit update action.
// Launch, skip and throttle policy belongs to UpdateController.
class UpdateDialog : public QDialog {
  Q_OBJECT

public:
  UpdateDialog(ServiceLocator &p_services, const UpdateInfo &p_info, QWidget *p_parent = nullptr);

  void setFailed(const QString &p_message);

#ifdef Q_OS_WIN
  void setUpdateInProgress();
#endif

signals:
  // "Skip this version": never offer this exact version again.
  void skipRequested(const QString &p_version);

#ifdef Q_OS_WIN
  void updateRequested();
#endif

private:
  void setupUi(ServiceLocator &p_services);

  const UpdateInfo m_info;

  QLabel *m_headline = nullptr;
  QLabel *m_summary = nullptr;
  QLabel *m_status = nullptr;

  QPushButton *m_releasePageButton = nullptr;
  QPushButton *m_skipButton = nullptr;
  QPushButton *m_laterButton = nullptr;
};

} // namespace vnotex

#endif // UPDATEDIALOG_H
