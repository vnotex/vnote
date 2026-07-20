#ifndef ENCODINGBUTTON_H
#define ENCODINGBUTTON_H

#include <QString>
#include <QToolButton>

namespace vnotex {

// A reusable, pure-view encoding picker button.
//
// EncodingButton is a QToolButton that pops a menu of text encodings: a
// curated shortlist first, then the full QTextCodec::availableCodecs() list.
// It holds NO Buffer2 and NO services — it only reflects the current encoding
// (setCurrentEncoding) and emits an intent (encodingChangeRequested) when the
// user selects one. The owning view/base is responsible for reacting.
class EncodingButton : public QToolButton {
  Q_OBJECT
public:
  explicit EncodingButton(QWidget *p_parent = nullptr);

  // Update the button label to reflect the active encoding. Does NOT emit
  // encodingChangeRequested (this is a display-only update).
  void setCurrentEncoding(const QString &p_codecName);

signals:
  // Emitted when the user picks an encoding from the menu.
  void encodingChangeRequested(const QString &p_codecName);

private:
  // Populate the menu on first show (deferred off the window-open path).
  void buildMenu();

  QString m_currentEncoding;
  bool m_menuBuilt = false;
};

} // namespace vnotex

#endif // ENCODINGBUTTON_H
