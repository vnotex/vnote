#ifndef SNIPPETPROPERTIESDIALOG2_H
#define SNIPPETPROPERTIESDIALOG2_H

#include "scrolldialog.h"

#include <QString>

namespace vnotex {
class ServiceLocator;
class SnippetInfoWidget2;
class SnippetController;

class SnippetPropertiesDialog2 : public ScrollDialog {
  Q_OBJECT
public:
  SnippetPropertiesDialog2(ServiceLocator &p_services, SnippetController *p_controller,
                           const QString &p_snippetName, QWidget *p_parent = nullptr);

signals:
  void snippetUpdated(const QString &p_name);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();
  bool validateInputs();
  bool saveSnippetProperties();

  ServiceLocator &m_services;
  SnippetController *m_controller = nullptr;
  QString m_originalName;
  int m_originalShortcut = -1;
  SnippetInfoWidget2 *m_infoWidget = nullptr;
};
} // namespace vnotex

#endif // SNIPPETPROPERTIESDIALOG2_H
