#ifndef NEWSNIPPETDIALOG2_H
#define NEWSNIPPETDIALOG2_H

#include "scrolldialog.h"

namespace vnotex {
class ServiceLocator;
class SnippetInfoWidget2;
class SnippetController;

class NewSnippetDialog2 : public ScrollDialog {
  Q_OBJECT
public:
  explicit NewSnippetDialog2(ServiceLocator &p_services, SnippetController *p_controller,
                             QWidget *p_parent = nullptr);

signals:
  void snippetCreated(const QString &p_name);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();
  bool validateInputs();
  bool newSnippet();

  ServiceLocator &m_services;
  SnippetController *m_controller = nullptr;
  SnippetInfoWidget2 *m_infoWidget = nullptr;
};
} // namespace vnotex

#endif // NEWSNIPPETDIALOG2_H
