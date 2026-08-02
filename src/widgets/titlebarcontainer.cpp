#include "titlebarcontainer.h"

#include <QEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleWidget>
#endif

using namespace vnotex;

namespace {

#if QT_CONFIG(accessibility)
// A QMenuBar subclass otherwise resolves to QAccessibleMenuBar, whose
// childCount() is actions().size() -- zero for this container -- which would
// hide the entire title-bar subtree from assistive technology. Map the exact
// class back to a generic QAccessibleWidget so child traversal is preserved,
// exactly as it was when the slot held a plain QWidget.
QAccessibleInterface *titleBarContainerAccessibleFactory(const QString &p_className,
                                                         QObject *p_object) {
  if (p_className == QLatin1String("vnotex::TitleBarContainer") && p_object &&
      p_object->isWidgetType()) {
    return new QAccessibleWidget(static_cast<QWidget *>(p_object), QAccessible::Grouping);
  }
  return nullptr;
}
#endif

// Install the factory exactly once, whichever instance is constructed first.
void ensureAccessibleFactoryInstalled() {
#if QT_CONFIG(accessibility)
  static const bool installed = []() {
    QAccessible::installFactory(titleBarContainerAccessibleFactory);
    return true;
  }();
  Q_UNUSED(installed);
#endif
}

} // namespace

TitleBarContainer::TitleBarContainer(QWidget *p_parent) : QMenuBar(p_parent) {
  // Under a global-menu platform theme (macOS, KDE appmenu) QMenuBarPrivate::init()
  // creates a platform menu bar and then hides this widget, which would make the
  // whole title bar invisible. Dropping the platform bar also calls setVisible(true)
  // because a parent is set.
  setNativeMenuBar(false);

  setFocusPolicy(Qt::NoFocus);

  ensureAccessibleFactoryInstalled();
}

int TitleBarContainer::heightForWidth(int p_width) const {
  auto *lay = layout();
  if (!lay) {
    return QMenuBar::heightForWidth(p_width);
  }

  // This is THE sizing path for the menu-widget slot (see the header comment),
  // so it must never return -1: QMenuBar::changeEvent feeds the result straight
  // into resize(), and QLayoutPrivate::doResize() falls back to an
  // action-derived height when it gets -1.
  if (lay->hasHeightForWidth()) {
    return lay->totalHeightForWidth(p_width);
  }
  return sizeHint().height();
}

QSize TitleBarContainer::sizeHint() const {
  // QMenuBar::sizeHint()/minimumSizeHint() compute from actions and ignore any
  // child layout. Replicate QWidget's default (layout-driven) behaviour, which
  // is what the plain QWidget in this slot used to provide.
  auto *lay = layout();
  return lay ? lay->totalSizeHint() : QMenuBar::sizeHint();
}

QSize TitleBarContainer::minimumSizeHint() const {
  auto *lay = layout();
  return lay ? lay->totalMinimumSize() : QMenuBar::minimumSizeHint();
}

void TitleBarContainer::changeEvent(QEvent *p_event) {
  // Skip QMenuBar::changeEvent(), which on QEvent::StyleChange self-resizes to
  // heightForWidth(parentWidget()->width()) and churns updateGeometries().
  // VNote re-applies the global stylesheet on every theme change, so this fires
  // in normal use. The resize itself is already harmless because
  // heightForWidth() above is layout-derived; this is the second line of defense
  // and it also skips recomputing geometry for actions that do not exist.
  //
  // Bypassing QMenuBar's ParentChange/FontChange handling is safe: there are no
  // actions and the container is never reparented.
  QWidget::changeEvent(p_event);
}

void TitleBarContainer::paintEvent(QPaintEvent *p_event) {
  Q_UNUSED(p_event);

  // Draw as a plain widget so the style does not paint PE_PanelMenuBar behind
  // the title bar. PE_Widget (rather than nothing) keeps global QSS backgrounds
  // working.
  QStyleOption opt;
  opt.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
}

bool TitleBarContainer::eventFilter(QObject *p_object, QEvent *p_event) {
  // QMenuBarPrivate::handleReparent() installs this widget as an event filter on
  // every ancestor up to the window to implement Alt-key navigation;
  // QMenuBar::eventFilter() would call setKeyboardMode() and steal focus into an
  // actionless menu bar.
  return QWidget::eventFilter(p_object, p_event);
}

void TitleBarContainer::mousePressEvent(QMouseEvent *p_event) { p_event->ignore(); }

void TitleBarContainer::mouseReleaseEvent(QMouseEvent *p_event) { p_event->ignore(); }

void TitleBarContainer::mouseMoveEvent(QMouseEvent *p_event) { p_event->ignore(); }
