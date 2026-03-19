#include "wordcountpopup.h"

#include <QFormLayout>
#include <QLabel>
#include <QPointer>

#include <utils/widgetutils.h>

using namespace vnotex;

WordCountPopup::WordCountPopup(QToolButton *p_btn, const ViewWindow *p_viewWindow,
                               QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent), m_viewWindow(p_viewWindow) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() {
    QPointer<WordCountPopup> popup(this);
    m_viewWindow->fetchWordCountInfo([popup](const ViewWindow::WordCountInfo &info) {
      if (popup) {
        popup->updateCount(info);
      }
    });
  });
}

void WordCountPopup::updateCount(const ViewWindow::WordCountInfo &p_info) {
  m_panel->updateCount(p_info.m_isSelection, p_info.m_wordCount, p_info.m_charWithoutSpaceCount,
                       p_info.m_charWithSpaceCount);
}

void WordCountPopup::setupUI() {
  m_panel = new WordCountPanel(this);
  addWidget(m_panel);
}
