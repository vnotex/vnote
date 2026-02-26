#ifndef SYSTEMTRAYHELPER_H
#define SYSTEMTRAYHELPER_H

class QSystemTrayIcon;

namespace vnotex {
class MainWindow2;
class ConfigMgr2;

class SystemTrayHelper {
public:
  SystemTrayHelper() = delete;

  static QSystemTrayIcon *setupSystemTray(MainWindow2 *p_win, const ConfigMgr2 *p_configMgr);
};
} // namespace vnotex

#endif // SYSTEMTRAYHELPER_H
