#ifndef VIEWTAGSDIALOG2_H
#define VIEWTAGSDIALOG2_H

#include <QList>
#include <QSet>
#include <QString>

#include <core/nodeidentifier.h>

#include "scrolldialog.h"

class QKeyEvent;
class QLabel;

namespace vnotex {

class ServiceLocator;
class TagViewer2;

class ViewTagsDialog2 : public ScrollDialog {
  Q_OBJECT
public:
  ViewTagsDialog2(ServiceLocator &p_services, const QList<NodeIdentifier> &p_nodeIds,
                  QWidget *p_parent = nullptr);

  // Tags the user turned ON for every target.
  QSet<QString> addedTags() const;

  // Tags the user turned OFF for every target.
  QSet<QString> removedTags() const;

protected:
  void keyPressEvent(QKeyEvent *p_event) override;

private:
  void setupUI();

  ServiceLocator &m_services;

  QLabel *m_nodeNameLabel = nullptr;

  TagViewer2 *m_tagViewer = nullptr;
};

} // namespace vnotex

#endif // VIEWTAGSDIALOG2_H
