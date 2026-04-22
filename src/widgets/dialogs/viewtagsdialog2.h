#ifndef VIEWTAGSDIALOG2_H
#define VIEWTAGSDIALOG2_H

#include "scrolldialog.h"

class QKeyEvent;
class QLabel;

namespace vnotex {

struct NodeIdentifier;
class ServiceLocator;
class TagViewer2;

class ViewTagsDialog2 : public ScrollDialog {
  Q_OBJECT
public:
  ViewTagsDialog2(ServiceLocator &p_services, const NodeIdentifier &p_nodeId,
                  QWidget *p_parent = nullptr);

protected:
  void acceptedButtonClicked() override;

  void keyPressEvent(QKeyEvent *p_event) override;

private:
  void setupUI();

  ServiceLocator &m_services;

  QLabel *m_nodeNameLabel = nullptr;

  TagViewer2 *m_tagViewer = nullptr;
};

} // namespace vnotex

#endif // VIEWTAGSDIALOG2_H
