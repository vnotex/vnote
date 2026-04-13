#ifndef FILEASSOCIATIONPAGE_H
#define FILEASSOCIATIONPAGE_H

#include "settingspage.h"

namespace vnotex {
class FileAssociationPage : public SettingsPage {
  Q_OBJECT
public:
  explicit FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void loadBuiltInTypesGroup(QWidget *p_container);

  void loadExternalProgramsGroup(QWidget *p_container);

  QWidget *m_builtInContainer = nullptr;

  QWidget *m_externalContainer = nullptr;

  static const char *c_nameProperty;

  static const QChar c_suffixSeparator;
};
} // namespace vnotex

#endif // FILEASSOCIATIONPAGE_H
