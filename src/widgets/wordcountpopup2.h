#ifndef WORDCOUNTPOPUP2_H
#define WORDCOUNTPOPUP2_H

#include "buttonpopup.h"

#include <functional>

class QToolButton;

namespace vnotex {
class WordCountPanel;

// New-architecture word count popup.
// Uses a fetch callback instead of depending on legacy ViewWindow.
class WordCountPopup2 : public ButtonPopup {
  Q_OBJECT
public:
  // Callback type: receives a WordCountPanel pointer and is responsible
  // for calling panel->updateCount(isSelection, words, charsNoSpace, charsWithSpace).
  using FetchCallback = std::function<void(WordCountPanel *)>;

  WordCountPopup2(QToolButton *p_btn, const FetchCallback &p_fetchCallback,
                  QWidget *p_parent = nullptr);

private:
  void setupUI();

  WordCountPanel *m_panel = nullptr;

  FetchCallback m_fetchCallback;
};
} // namespace vnotex

#endif // WORDCOUNTPOPUP2_H
