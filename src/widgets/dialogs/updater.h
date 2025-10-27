#ifndef UPDATER_H
#define UPDATER_H

#include "dialog.h"

#include <functional>

class QLabel;

namespace vnotex {
class Updater : public Dialog {
  Q_OBJECT
public:
  explicit Updater(QWidget *p_parent = nullptr);

  // Callback(hasUpdate, VersionOnSuccess, errMsg).
  static void
  checkForUpdates(QObject *p_receiver,
                  const std::function<void(bool, const QString &, const QString &)> &p_callback);

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
  void start();

private:
  void setupUI();

  QLabel *m_latestVersionLabel = nullptr;
};
} // namespace vnotex

#endif // UPDATER_H
