#ifndef NEWIMAGEHOSTDIALOG_H
#define NEWIMAGEHOSTDIALOG_H

#include "../scrolldialog.h"

class QComboBox;
class QLineEdit;

namespace vnotex {
class ImageHost;

class NewImageHostDialog : public ScrollDialog {
  Q_OBJECT
public:
  explicit NewImageHostDialog(QWidget *p_parent = nullptr);

  ImageHost *getNewImageHost() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  bool validateInputs();

  bool newImageHost();

  QComboBox *m_typeComboBox = nullptr;

  QLineEdit *m_nameLineEdit = nullptr;

  ImageHost *m_imageHost = nullptr;
};
} // namespace vnotex

#endif // NEWIMAGEHOSTDIALOG_H
