#include "titletoolbar2.h"

#include <QHBoxLayout>
#include <QToolButton>

#include "propertydefs.h"

using namespace vnotex;

TitleToolBar2::TitleToolBar2(QWidget *p_parent) : QToolBar(p_parent), m_window(p_parent) {
  setupUI();
}

TitleToolBar2::TitleToolBar2(const QString &p_title, QWidget *p_parent)
    : QToolBar(p_title, p_parent), m_window(p_parent) {
  setupUI();
}

void TitleToolBar2::setupUI() {}

void TitleToolBar2::maximizeRestoreWindow() {
  m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
}

QWidget *TitleToolBar2::createTitleBarButtons(const QIcon &p_minimizeIcon,
                                              const QIcon &p_maximizeIcon,
                                              const QIcon &p_restoreIcon,
                                              const QIcon &p_closeIcon) {
  m_titleButtonsContainer = new QWidget();
  auto *containerLayout = new QHBoxLayout(m_titleButtonsContainer);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  m_minimizeBtn = new QToolButton(m_titleButtonsContainer);
  m_minimizeBtn->setIcon(p_minimizeIcon);
  m_minimizeBtn->setToolTip(tr("Minimize"));
  m_minimizeBtn->setAutoRaise(true);
  containerLayout->addWidget(m_minimizeBtn);
  connect(m_minimizeBtn, &QToolButton::clicked, this, [this]() { m_window->showMinimized(); });

  m_maximizeIcon = p_maximizeIcon;
  m_restoreIcon = p_restoreIcon;
  m_maximizeBtn = new QToolButton(m_titleButtonsContainer);
  m_maximizeBtn->setIcon(p_maximizeIcon);
  m_maximizeBtn->setToolTip(tr("Maximize"));
  m_maximizeBtn->setAutoRaise(true);
  containerLayout->addWidget(m_maximizeBtn);
  connect(m_maximizeBtn, &QToolButton::clicked, this, [this]() { maximizeRestoreWindow(); });

  m_closeBtn = new QToolButton(m_titleButtonsContainer);
  m_closeBtn->setIcon(p_closeIcon);
  m_closeBtn->setToolTip(tr("Close"));
  m_closeBtn->setAutoRaise(true);
  m_closeBtn->setProperty(PropertyDefs::c_dangerButton, true);
  containerLayout->addWidget(m_closeBtn);
  connect(m_closeBtn, &QToolButton::clicked, this, [this]() { m_window->close(); });

  updateMaximizeButton();

  return m_titleButtonsContainer;
}

void TitleToolBar2::updateMaximizeButton() {
  if (m_window->isMaximized()) {
    m_maximizeBtn->setIcon(m_restoreIcon);
    m_maximizeBtn->setToolTip(tr("Restore Down"));
  } else {
    m_maximizeBtn->setIcon(m_maximizeIcon);
    m_maximizeBtn->setToolTip(tr("Maximize"));
  }
}

void TitleToolBar2::refreshIcons(const QIcon &p_minimizeIcon, const QIcon &p_maximizeIcon,
                                 const QIcon &p_restoreIcon, const QIcon &p_closeIcon) {
  if (m_minimizeBtn) {
    m_minimizeBtn->setIcon(p_minimizeIcon);
  }
  m_maximizeIcon = p_maximizeIcon;
  m_restoreIcon = p_restoreIcon;
  if (m_closeBtn) {
    m_closeBtn->setIcon(p_closeIcon);
  }
  updateMaximizeButton();
}

QToolButton *TitleToolBar2::minimizeButton() const { return m_minimizeBtn; }

QToolButton *TitleToolBar2::maximizeButton() const { return m_maximizeBtn; }

QToolButton *TitleToolBar2::closeButton() const { return m_closeBtn; }
