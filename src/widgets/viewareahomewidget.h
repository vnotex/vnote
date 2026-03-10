#ifndef VIEWAREAHOMEWIDGET_H
#define VIEWAREAHOMEWIDGET_H

#include <QWidget>

namespace vnotex {

// Placeholder home screen shown in ViewArea2 when no ViewWindows are open.
class ViewAreaHomeWidget : public QWidget {
  Q_OBJECT
public:
  explicit ViewAreaHomeWidget(QWidget *p_parent = nullptr);
};

} // namespace vnotex

#endif // VIEWAREAHOMEWIDGET_H
