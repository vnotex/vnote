#include "titletoolbar.h"

#include <QToolButton>

#include "propertydefs.h"

using namespace vnotex;

TitleToolBar::TitleToolBar(QWidget *p_parent) : QToolBar(p_parent), m_window(p_parent) {
  setupUI();
}

TitleToolBar::TitleToolBar(const QString &p_title, QWidget *p_parent)
    : QToolBar(p_title, p_parent), m_window(p_parent) {
  setupUI();
}

void TitleToolBar::setupUI() {}

void TitleToolBar::maximizeRestoreWindow() {
  m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
}

void TitleToolBar::addTitleBarIcons(const QIcon &p_minimizeIcon, const QIcon &p_maximizeIcon,
                                    const QIcon &p_restoreIcon, const QIcon &p_closeIcon) {
  addSeparator();

  addAction(p_minimizeIcon, tr("Minimize"), this, [this]() { m_window->showMinimized(); });

  m_maximizeIcon = p_maximizeIcon;
  m_restoreIcon = p_restoreIcon;
  m_maximizeAct =
      addAction(p_maximizeIcon, tr("Maximize"), this, [this]() { maximizeRestoreWindow(); });

  {
    auto closeAct = addAction(p_closeIcon, tr("Close"), this, [this]() { m_window->close(); });
    auto btn = static_cast<QToolButton *>(widgetForAction(closeAct));
    btn->setProperty(PropertyDefs::c_dangerButton, true);
  }

  updateMaximizeAct();
}

void TitleToolBar::updateMaximizeAct() {
  if (m_window->isMaximized()) {
    m_maximizeAct->setIcon(m_restoreIcon);
    m_maximizeAct->setText(tr("Restore Down"));
  } else {
    m_maximizeAct->setIcon(m_maximizeIcon);
    m_maximizeAct->setText(tr("Maximize"));
  }
}
