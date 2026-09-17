#ifndef NOTEBOOKINFOWIDGET_H
#define NOTEBOOKINFOWIDGET_H

#include <QWidget>

#include <core/services/notebookcoreservice.h>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace vnotex {
class LocationInputWithBrowseButton;
class ServiceLocator;
struct NotebookInfo;

// Shared notebook form. Controllers own validation and persistence.
class NotebookInfoWidget : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Create, Edit };

  NotebookInfoWidget(ServiceLocator &p_services, Mode p_mode, QWidget *p_parent = nullptr);

  // Loading/resetting an existing notebook never emits inputEdited().
  // An empty id clears the form and disables editing.
  void setNotebookInfo(const NotebookInfo &p_info);

  QString getName() const;
  QString getDescription() const;
  QString getRootFolder() const;
  NotebookType getType() const;
  QString getAssetsFolder() const;
  QString getRecycleBinFolder() const;
  QString getLineEnding() const;

signals:
  void inputEdited();
  void typeChanged(NotebookType p_type);

private:
  void setupUI(ServiceLocator &p_services);
  void updateEditability();

  const Mode m_mode;
  bool m_hasNotebook = false;
  bool m_readOnly = false;
  QLineEdit *m_nameEdit = nullptr;
  QPlainTextEdit *m_descriptionEdit = nullptr;
  LocationInputWithBrowseButton *m_rootFolderInput = nullptr;
  QPushButton *m_openRootFolderButton = nullptr;
  QComboBox *m_typeComboBox = nullptr;
  QLineEdit *m_assetsFolderEdit = nullptr;
  LocationInputWithBrowseButton *m_recycleBinFolderInput = nullptr;
  QComboBox *m_lineEndingComboBox = nullptr;
};
} // namespace vnotex

#endif // NOTEBOOKINFOWIDGET_H
