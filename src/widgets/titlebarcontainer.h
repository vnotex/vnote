#ifndef TITLEBARCONTAINER_H
#define TITLEBARCONTAINER_H

#include <QMenuBar>

QT_BEGIN_NAMESPACE
class QEvent;
class QMouseEvent;
class QPaintEvent;
QT_END_NAMESPACE

namespace vnotex {

// The host widget for the frameless-mode title bar, installed into
// QMainWindow's menu-widget slot via QMainWindow::setMenuWidget().
//
// THIS IS A CONTAINER, NOT A MENU BAR. NEVER add a QAction to it. Every
// override below assumes the "no actions, ever" invariant.
//
// WHY IT DERIVES FROM QMenuBar (issue #2722)
// ------------------------------------------
// The menu-widget slot must hold something that QMainWindow::menuBar() can
// qobject_cast to a QMenuBar. Qt's accessor is:
//
//   QMenuBar *QMainWindow::menuBar() const {
//     QMenuBar *menuBar = qobject_cast<QMenuBar *>(layout()->menuBar());
//     if (!menuBar) { ... new QMenuBar(self); self->setMenuBar(menuBar); }
//
// and setMenuBar() evicts whatever occupied the slot:
// `existingMenuBar->hide(); setParent(nullptr); deleteLater();`.
//
// KDE Breeze's ToolsAreaManager::registerWidget() calls mainWindow->menuBar()
// on every QMainWindow it polishes, so with a plain QWidget in the slot the
// whole title bar (toolbar, buttons, menus, actions) was destroyed on the next
// event-loop pass — and ToolBarHelper2 then dereferenced the freed QActions,
// giving the SIGSEGV reported in #2722. Deriving from QMenuBar makes the cast
// succeed, so Qt never reclaims the slot. Breeze's tools-area painting is
// unaffected: toolsAreaRect() reads menuWidget(), not menuBar().
//
// LOAD-BEARING OVERRIDES
// ----------------------
// * heightForWidth() is THE sizing path for the menu-widget slot.
//   QMainWindowLayout has no menu-bar special-casing, so QLayoutPrivate::doResize
//   sizes the slot via menuBarHeightForWidth(), which prefers
//   heightForWidth(w) and only falls back to sizeHint().height() when that
//   returns -1. QMenuBar's implementation derives a height from its *actions*,
//   which for an actionless bar collapses the title bar to a few pixels.
// * changeEvent() does not reach QMenuBar's, which self-resizes on
//   QEvent::StyleChange to heightForWidth(parentWidget()->width()) and runs
//   updateGeometries(). VNote re-applies the global stylesheet on every theme
//   change, so it fires in normal use. Measured: with heightForWidth() above
//   overridden that resize is already benign, so this is a second line of
//   defense that also skips the pointless action-geometry recomputation.
// * eventFilter() must NOT reach QMenuBar's: QMenuBarPrivate::handleReparent()
//   installs the menu bar as an event filter on every ancestor up to the window
//   to implement Alt-key navigation, and setKeyboardMode() would steal focus
//   into an actionless menu bar.
// * setNativeMenuBar(false) in the constructor: under a global-menu platform
//   theme (macOS, KDE appmenu) QMenuBarPrivate::init() creates a platform menu
//   bar and then hides the widget, which would make the title bar invisible.
//
// DELIBERATELY NOT OVERRIDDEN, because the "never add actions" invariant leaves
// them with nothing to do: QMenuBarExtension (stays hidden with nothing to
// overflow), actionEvent(), mnemonic/shortcut registration, key handling, and
// updateGeometries() (Qt may still run it, but there are no action rects to
// compute, so the work is harmless).
//
// ACCESSIBILITY: a QMenuBar subclass would otherwise resolve to
// QAccessibleMenuBar, whose childCount() is actions().size() == 0, hiding the
// whole toolbar subtree from assistive technology. titlebarcontainer.cpp
// installs a QAccessible factory that maps this exact class back to a generic
// QAccessibleWidget (QAccessible::Grouping). That match is on
// metaObject()->className(), which is why Q_OBJECT below is mandatory even
// though this class declares no signals or slots.
class TitleBarContainer : public QMenuBar {
  Q_OBJECT

public:
  explicit TitleBarContainer(QWidget *p_parent = nullptr);

  // Layout-derived, never -1: QMenuBar::changeEvent feeds this straight into
  // resize() and the menu-widget slot sizes itself from it.
  int heightForWidth(int p_width) const override;

  QSize sizeHint() const override;

  QSize minimumSizeHint() const override;

protected:
  void changeEvent(QEvent *p_event) override;

  void paintEvent(QPaintEvent *p_event) override;

  bool eventFilter(QObject *p_object, QEvent *p_event) override;

  void mousePressEvent(QMouseEvent *p_event) override;

  void mouseReleaseEvent(QMouseEvent *p_event) override;

  void mouseMoveEvent(QMouseEvent *p_event) override;
};

} // namespace vnotex

#endif // TITLEBARCONTAINER_H
