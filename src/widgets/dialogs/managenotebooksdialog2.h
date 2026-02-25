#ifndef MANAGENOTEBOOKSDIALOG2_H
#define MANAGENOTEBOOKSDIALOG2_H

#include "dialog.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace vnotex {

class ManageNotebooksController;
class ServiceLocator;

// ManageNotebooksDialog2 - Dialog for managing notebooks using DI architecture.
// Allows viewing, editing (name/description), closing, and deleting notebooks.
// Uses ManageNotebooksController for business logic.
class ManageNotebooksDialog2 : public Dialog {
  Q_OBJECT

public:
  // Constructor with optional pre-selection of a notebook.
  // p_currentNotebookId: ID of notebook to select initially (empty = select first).
  explicit ManageNotebooksDialog2(ServiceLocator &p_services,
                                  const QString &p_currentNotebookId = QString(),
                                  QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

  void resetButtonClicked() Q_DECL_OVERRIDE;

  void appliedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void onCurrentNotebookChanged(QListWidgetItem *p_current, QListWidgetItem *p_previous);

  void openRootFolderInExplorer();

private:
  void setupUI();

  void loadNotebooks();

  void selectNotebook(const QString &p_notebookId);

  void setChangesUnsaved(bool p_unsaved);

  bool saveChangesToNotebook();

  bool checkUnsavedChanges();

  void closeSelectedNotebook();

  // Get notebook ID from list item.
  QString getNotebookIdFromItem(const QListWidgetItem *p_item) const;

  ServiceLocator &m_services;

  ManageNotebooksController *m_controller = nullptr;

  // ID of notebook to select on load.
  QString m_initialNotebookId;

  // Currently selected notebook ID.
  QString m_currentNotebookId;

  // UI widgets.
  QListWidget *m_notebookList = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QPlainTextEdit *m_descriptionEdit = nullptr;
  QLineEdit *m_rootFolderEdit = nullptr;
  QLabel *m_typeLabel = nullptr;
  QPushButton *m_closeBtn = nullptr;

  bool m_changesUnsaved = false;
};

} // namespace vnotex

#endif // MANAGENOTEBOOKSDIALOG2_H
