#ifndef FILEASSOCIATIONPAGE_H
#define FILEASSOCIATIONPAGE_H

#include "settingspage.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vnotex {
#ifdef Q_OS_WIN
class WindowsContextMenuController;
#endif

class FileAssociationPage : public SettingsPage {
  Q_OBJECT
public:
  explicit FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

#ifdef Q_OS_WIN
  void setupSystemContextMenu(QVBoxLayout *p_layout);
  void refreshSystemContextMenu();
#endif

  void loadBuiltInTypesGroup();

  void loadExternalProgramsGroup();

  void addExternalProgramRow(const QString &p_name, const QString &p_command,
                             const QString &p_suffixes);

  void removeExternalProgramRow(QWidget *p_row);

  void addSystemProgramRow(const QString &p_suffixes);

  QVBoxLayout *m_builtInCardLayout = nullptr;
  int m_builtInRowStartIndex = 0;

  QVBoxLayout *m_externalCardLayout = nullptr;
  int m_externalRowStartIndex = 0;

  QPushButton *m_addProgramButton = nullptr;

#ifdef Q_OS_WIN
  WindowsContextMenuController *m_contextMenuController = nullptr;
  QPushButton *m_addContextMenuButton = nullptr;
  QPushButton *m_removeContextMenuButton = nullptr;
  QLabel *m_contextMenuStatus = nullptr;
#endif

  static const char *c_nameProperty;

  static const QChar c_suffixSeparator;
};
} // namespace vnotex

#endif // FILEASSOCIATIONPAGE_H
