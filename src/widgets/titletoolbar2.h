#ifndef TITLETOOLBAR2_H
#define TITLETOOLBAR2_H

#include <QIcon>
#include <QToolBar>

class QToolButton;

namespace vnotex {
class TitleToolBar2 : public QToolBar {
  Q_OBJECT
public:
  explicit TitleToolBar2(QWidget *p_parent = nullptr);

  TitleToolBar2(const QString &p_title, QWidget *p_parent = nullptr);

  QWidget *createTitleBarButtons(const QIcon &p_minimizeIcon, const QIcon &p_maximizeIcon,
                                 const QIcon &p_restoreIcon, const QIcon &p_closeIcon);

  void updateMaximizeButton();

  void refreshIcons(const QIcon &p_minimizeIcon, const QIcon &p_maximizeIcon,
                    const QIcon &p_restoreIcon, const QIcon &p_closeIcon);

  QToolButton *minimizeButton() const;

  QToolButton *maximizeButton() const;

  QToolButton *closeButton() const;

private:
  void setupUI();

  void maximizeRestoreWindow();

  QWidget *m_window = nullptr;

  QWidget *m_titleButtonsContainer = nullptr;

  QToolButton *m_minimizeBtn = nullptr;

  QToolButton *m_maximizeBtn = nullptr;

  QToolButton *m_closeBtn = nullptr;

  QIcon m_maximizeIcon;

  QIcon m_restoreIcon;
};
} // namespace vnotex

#endif // TITLETOOLBAR2_H
