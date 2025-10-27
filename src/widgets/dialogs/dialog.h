#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include <QDialogButtonBox>

class QBoxLayout;
class QPlainTextEdit;

namespace vnotex {
class Dialog : public QDialog {
  Q_OBJECT
public:
  explicit Dialog(QWidget *p_parent = nullptr, Qt::WindowFlags p_flags = Qt::WindowFlags());

  void
  setDialogButtonBox(QDialogButtonBox::StandardButtons p_buttons,
                     QDialogButtonBox::StandardButton p_defaultButton = QDialogButtonBox::NoButton);

  QDialogButtonBox *getDialogButtonBox() const;

  enum class InformationLevel { Info, Warning, Error };

  void setInformationText(const QString &p_text, InformationLevel p_level = InformationLevel::Info);

  void appendInformationText(const QString &p_text);

  void clearInformationText();

  void setButtonEnabled(QDialogButtonBox::StandardButton p_button, bool p_enabled);

  // Dialog has completed but just stay the GUI to let user know information.
  void completeButStay();

  bool isCompleted() const;

  QSize sizeHint() const Q_DECL_OVERRIDE;

protected:
  virtual void acceptedButtonClicked();

  virtual void rejectedButtonClicked();

  virtual void resetButtonClicked();

  virtual void appliedButtonClicked();

  virtual void setCentralWidget(QWidget *p_widget);

  // Add @p_widget below the central widget.
  virtual void addBottomWidget(QWidget *p_widget);

  QBoxLayout *m_layout = nullptr;

  QWidget *m_centralWidget = nullptr;

private:
  QPlainTextEdit *m_infoTextEdit = nullptr;

  QDialogButtonBox *m_dialogButtonBox = nullptr;

  bool m_completed = false;
};
} // namespace vnotex

#endif // DIALOG_H
