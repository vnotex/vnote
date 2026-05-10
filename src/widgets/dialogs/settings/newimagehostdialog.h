#ifndef NEWIMAGEHOSTDIALOG_H
#define NEWIMAGEHOSTDIALOG_H

#include "../scrolldialog.h"

class QComboBox;
class QLineEdit;

namespace vnotex {
class ImageHostController;
class IImageHostProvider;

class NewImageHostDialog : public ScrollDialog {
  Q_OBJECT
public:
  explicit NewImageHostDialog(ImageHostController *p_controller, QWidget *p_parent = nullptr);

  IImageHostProvider *getNewProvider() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  bool validateInputs();

  bool newImageHost();

  ImageHostController *m_controller = nullptr;

  QComboBox *m_typeComboBox = nullptr;

  QLineEdit *m_nameLineEdit = nullptr;

  IImageHostProvider *m_newProvider = nullptr;
};
} // namespace vnotex

#endif // NEWIMAGEHOSTDIALOG_H
