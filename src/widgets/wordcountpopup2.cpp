#include "wordcountpopup2.h"

#include "wordcountpanel.h"

using namespace vnotex;

WordCountPopup2::WordCountPopup2(QToolButton *p_btn, const FetchCallback &p_fetchCallback,
                                 QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent), m_fetchCallback(p_fetchCallback) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() {
    if (m_fetchCallback) {
      m_fetchCallback(m_panel);
    }
  });
}

void WordCountPopup2::setupUI() {
  m_panel = new WordCountPanel(this);
  addWidget(m_panel);
}
