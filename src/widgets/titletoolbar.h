#ifndef TITLETOOLBAR_H
#define TITLETOOLBAR_H

#include <QIcon>
#include <QToolBar>

namespace vnotex {
class TitleToolBar : public QToolBar {
  Q_OBJECT
public:
  explicit TitleToolBar(QWidget *p_parent = nullptr);

  TitleToolBar(const QString &p_title, QWidget *p_parent = nullptr);

  void addTitleBarIcons(const QIcon &p_minimizeIcon, const QIcon &p_maximizeIcon,
                        const QIcon &p_restoreIcon, const QIcon &p_closeIcon);

  void updateMaximizeAct();

private:
  void setupUI();

  void maximizeRestoreWindow();

  QWidget *m_window = nullptr;

  QAction *m_maximizeAct = nullptr;

  QIcon m_maximizeIcon;

  QIcon m_restoreIcon;
};
} // namespace vnotex

#endif // VTOOLBAR_H
