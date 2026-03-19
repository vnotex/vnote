#ifndef WORDCOUNTPOPUP_H
#define WORDCOUNTPOPUP_H

#include "buttonpopup.h"

#include "viewwindow.h"
#include "wordcountpanel.h"

class QToolButton;

namespace vnotex {

class WordCountPopup : public ButtonPopup {
  Q_OBJECT
public:
  WordCountPopup(QToolButton *p_btn, const ViewWindow *p_viewWindow, QWidget *p_parent = nullptr);

  void updateCount(const ViewWindow::WordCountInfo &p_info);

private:
  void setupUI();

  WordCountPanel *m_panel = nullptr;

  const ViewWindow *m_viewWindow = nullptr;
};
} // namespace vnotex

#endif // WORDCOUNTPOPUP_H
