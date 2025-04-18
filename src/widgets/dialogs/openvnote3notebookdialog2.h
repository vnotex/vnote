#ifndef OPENVNOTE3NOTEBOOKDIALOG2_H
#define OPENVNOTE3NOTEBOOKDIALOG2_H

#include "scrolldialog.h"

#include <QString>

class QCheckBox;
class QLabel;

namespace vnotex {

class LocationInputWithBrowseButton;
class OpenVNote3NotebookController;
class ServiceLocator;

class OpenVNote3NotebookDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  explicit OpenVNote3NotebookDialog2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~OpenVNote3NotebookDialog2() override;

  QString getOpenedNotebookId() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void validateInputs();

private:
  void setupUI();

  ServiceLocator &m_services;

  OpenVNote3NotebookController *m_controller = nullptr;

  LocationInputWithBrowseButton *m_sourceInput = nullptr;
  LocationInputWithBrowseButton *m_destinationInput = nullptr;
  QLabel *m_nameTitleLabel = nullptr;
  QLabel *m_nameLabel = nullptr;
  QLabel *m_descriptionTitleLabel = nullptr;
  QLabel *m_descriptionLabel = nullptr;
  QLabel *m_warningsTitleLabel = nullptr;
  QLabel *m_warningsLabel = nullptr;
  QCheckBox *m_confirmCheckBox = nullptr;

  QString m_openedNotebookId;

  bool m_destinationManuallyEdited = false;
  bool m_suppressDestinationTracking = false;
};

} // namespace vnotex

#endif // OPENVNOTE3NOTEBOOKDIALOG2_H
