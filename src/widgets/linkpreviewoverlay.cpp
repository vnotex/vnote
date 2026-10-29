#include "linkpreviewoverlay.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include "propertydefs.h"
#include "viewsplit2.h"
#include "viewwindow2.h"
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

using namespace vnotex;

namespace {
bool isSourceSurface(QWidget *p_source, QWidget *p_receiver) {
  if (!p_source || !p_receiver || !p_source->isVisible() || !p_source->isEnabled() ||
      p_receiver->window() != p_source->window()) {
    return false;
  }
  for (auto *widget = p_receiver; widget; widget = widget->parentWidget()) {
    if (widget == p_source) {
      return true;
    }
    if (qobject_cast<LinkPreviewOverlay *>(widget)) {
      return false;
    }
  }
  return false;
}

// The preview can retire before the platform delivers a double-click's second
// press. Keep only a short-lived source guard, never a workspace or tab owner.
class PreviewDismissalTail final : public QObject {
public:
  PreviewDismissalTail(QWidget *p_source, const QPoint &p_position)
      : QObject(p_source), m_source(p_source), m_position(p_position) {
    qApp->installEventFilter(this);
    QTimer::singleShot(QApplication::doubleClickInterval(), this, [this] { stop(); });
  }

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override {
    const auto type = p_event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
        type != QEvent::MouseButtonRelease && type != QEvent::MouseMove) {
      return false;
    }
    auto *widget = qobject_cast<QWidget *>(p_object);
    if (!widget) {
      return false;
    }
    if (!m_source || QApplication::activeModalWidget() || QApplication::activePopupWidget() ||
        type == QEvent::MouseButtonPress) {
      stop();
      return false;
    }
    const auto *mouse = static_cast<QMouseEvent *>(p_event);
    if (type == QEvent::MouseButtonDblClick) {
      const auto position = mouse->globalPosition().toPoint();
      const auto delta = position - m_position;
      const int distance = QApplication::styleHints()->mouseDoubleClickDistance();
      if (mouse->button() != Qt::LeftButton || !isSourceSurface(m_source, widget) ||
          !m_source->rect().contains(m_source->mapFromGlobal(position)) ||
          qAbs(delta.x()) > distance || qAbs(delta.y()) > distance) {
        stop();
        return false;
      }
      m_releasePending = true;
      p_event->accept();
      return true;
    }
    if (m_releasePending && (type == QEvent::MouseMove || mouse->button() == Qt::LeftButton)) {
      p_event->accept();
      if (type == QEvent::MouseButtonRelease) {
        stop();
      }
      return true;
    }
    return false;
  }

private:
  void stop() {
    qApp->removeEventFilter(this);
    deleteLater();
  }

  QPointer<QWidget> m_source;
  QPoint m_position;
  bool m_releasePending = false;
};
} // namespace

LinkPreviewOverlay::LinkPreviewOverlay(ServiceLocator &p_services, ViewWindow2 *p_source,
                                       const QString &p_workspaceId)
    : QFrame(p_source) {
  Q_ASSERT(p_source);
  setObjectName(QStringLiteral("linkPreviewOverlay"));
  setFrameShape(QFrame::StyledPanel);
  setFrameShadow(QFrame::Raised);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_WIN
  // An interactive native child must cover late-created WebEngine HWNDs.
  // Unlike StatusOverlay, this surface must retain ordinary native hit testing.
  setAttribute(Qt::WA_NativeWindow);
#endif

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  // Even a tiny source controls the geometry; child size hints must not enlarge
  // this surface or impose a minimum extent on the source's layout.
  layout->setSizeConstraint(QLayout::SetNoConstraint);
  setMinimumSize(0, 0);

  auto *header = new QWidget(this);
  header->setObjectName(QStringLiteral("linkPreviewHeader"));
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(8, 4, 8, 4);
  auto *label = new QLabel(tr("Preview"), header);
  label->setObjectName(QStringLiteral("linkPreviewTitle"));
  headerLayout->addWidget(label);
  headerLayout->addStretch();

  auto *openButton = new QToolButton(header);
  openButton->setObjectName(QStringLiteral("openLinkPreviewInNewTabsButton"));
  openButton->setProperty(PropertyDefs::c_actionToolButton, true);
  openButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  auto *openAction = new QAction(tr("Open in New Tab"), openButton);
  openAction->setObjectName(QStringLiteral("openLinkPreviewInNewTabsAction"));
  openAction->setToolTip(tr("Open in New Tab"));
  openButton->setDefaultAction(openAction);
  connect(openAction, &QAction::triggered, this, &LinkPreviewOverlay::openInNewTabsRequested);
  headerLayout->addWidget(openButton);

  auto *closeButton = new QToolButton(header);
  closeButton->setObjectName(QStringLiteral("closeLinkPreviewButton"));
  closeButton->setProperty(PropertyDefs::c_actionToolButton, true);
  closeButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  auto *closeAction = new QAction(tr("Close Preview"), closeButton);
  closeAction->setObjectName(QStringLiteral("closeLinkPreviewAction"));
  closeButton->setDefaultAction(closeAction);
  connect(closeAction, &QAction::triggered, this, &LinkPreviewOverlay::closeRequested);

  auto *themeService = p_services.get<ThemeService>();
  const auto refreshIcons = [openAction, closeAction, themeService] {
    if (themeService) {
      const auto fg =
          themeService->paletteColor(QStringLiteral("widgets#viewsplit#action_button#fg"));
      openAction->setIcon(IconUtils::fetchIcon(
          themeService->getIconFile(QStringLiteral("open_in_new_tab.svg")), fg));
      closeAction->setIcon(
          IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("close.svg")), fg));
    } else {
      openAction->setIcon(IconUtils::fetchIcon(
          QStringLiteral(":/vnotex/data/core/icons/open_in_new_tab.svg")));
      closeAction->setIcon(
          IconUtils::fetchIcon(QStringLiteral(":/vnotex/data/core/icons/close.svg")));
    }
  };
  refreshIcons();
  if (themeService) {
    connect(themeService, &ThemeService::themeChanged, this, refreshIcons);
  }
  headerLayout->addWidget(closeButton);
  layout->addWidget(header);

  m_viewSplit = new ViewSplit2(p_services, p_workspaceId, this);
  m_viewSplit->setObjectName(QStringLiteral("linkPreviewSplit"));
  layout->addWidget(m_viewSplit, 1);
}

ViewSplit2 *LinkPreviewOverlay::getViewSplit() const { return m_viewSplit; }

void LinkPreviewOverlay::setAnchorRect(const QRect &p_rect) {
  const QRect area = p_rect.intersected(parentWidget()->contentsRect());
  const int width = qMax(0, area.width());
  const int height = qMax(0, area.height());
  const int horizontalInset = qMin(16, width / 2);
  const int verticalInset = qMin(16, height / 2);
  const int previewWidth = (width - 2 * horizontalInset) * 9 / 10;
  const int previewHeight = (height - 2 * verticalInset) * 9 / 10;
  const QRect target(area.x() + (width - previewWidth) / 2, area.y() + (height - previewHeight) / 2,
                     previewWidth, previewHeight);
  if (geometry() != target) {
    setGeometry(target);
  }
  raiseSurface();
  scheduleRaise();
}

bool LinkPreviewOverlay::event(QEvent *p_event) {
  const bool handled = QFrame::event(p_event);
  switch (p_event->type()) {
  case QEvent::Show:
    qApp->installEventFilter(this);
    raiseSurface();
    scheduleRaise();
    emit visibilityChanged(true);
    break;
  case QEvent::Hide:
    qApp->removeEventFilter(this);
    m_dismissalPressActive = false;
    m_dismissalClickCancelled = false;
    emit visibilityChanged(false);
    break;
  case QEvent::StyleChange:
  case QEvent::PaletteChange:
  case QEvent::FontChange:
    scheduleRaise();
    break;
  case QEvent::WinIdChange:
    scheduleRaise();
    break;
  default:
    break;
  }
  return handled;
}

bool LinkPreviewOverlay::eventFilter(QObject *p_object, QEvent *p_event) {
  const auto type = p_event->type();
  if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
      type != QEvent::MouseButtonRelease && type != QEvent::MouseMove) {
    return false;
  }
  auto *widget = qobject_cast<QWidget *>(p_object);
  if (!widget) {
    // Native QWindow delivery must reach the QWidget dispatch/grab machinery.
    return false;
  }
  if (!isVisible() || QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
    m_dismissalPressActive = false;
    return false;
  }
  const auto *mouse = static_cast<QMouseEvent *>(p_event);
  const auto position = mouse->globalPosition().toPoint();
  if (type == QEvent::MouseMove) {
    if (!m_dismissalPressActive) {
      return false;
    }
    if (!(mouse->buttons() & Qt::LeftButton)) {
      m_dismissalPressActive = false;
      return false;
    }
    if ((position - m_dismissalPressPosition).manhattanLength() >
        QApplication::startDragDistance()) {
      m_dismissalClickCancelled = true;
    }
    p_event->accept();
    return true;
  }

  auto *source = parentWidget();
  const bool outside = isSourceSurface(source, widget) &&
                       source->rect().contains(source->mapFromGlobal(position)) &&
                       !rect().contains(mapFromGlobal(position));
  if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
    m_dismissalPressActive = false;
    if (mouse->button() != Qt::LeftButton || !outside) {
      return false;
    }
    m_dismissalPressPosition = position;
    m_dismissalPressActive = true;
    // A surviving/cancelled preview must not issue another close for the tail.
    m_dismissalClickCancelled = type == QEvent::MouseButtonDblClick;
    p_event->accept();
    // Qt may focus the source before application filters run. Keep consent and
    // subsequent typing on the preview if the user cancels dismissal.
    m_viewSplit->focus();
    return true;
  }
  if (mouse->button() != Qt::LeftButton || !m_dismissalPressActive) {
    return false;
  }

  const bool dismiss =
      outside && !m_dismissalClickCancelled &&
      (position - m_dismissalPressPosition).manhattanLength() <= QApplication::startDragDistance();
  m_dismissalPressActive = false;
  m_dismissalClickCancelled = false;
  p_event->accept();
  if (dismiss) {
    const QPointer<QWidget> guardedSource(source);
    const QPointer<LinkPreviewOverlay> guardedOverlay(this);
    emit closeRequested();
    // Close hooks/modal loops may have destroyed this overlay or its source.
    if (guardedSource) {
      new PreviewDismissalTail(guardedSource, position);
      if (guardedSource->isVisible() && guardedSource->isEnabled() &&
          !QApplication::activeModalWidget() && !QApplication::activePopupWidget() &&
          (!guardedOverlay || guardedOverlay->getViewSplit()->getViewWindowCount() == 0)) {
        guardedSource->setFocus(Qt::MouseFocusReason);
      }
    }
  }
  return true;
}

void LinkPreviewOverlay::mousePressEvent(QMouseEvent *p_event) {
  if (p_event->button() == Qt::LeftButton && m_viewSplit) {
    m_viewSplit->focus();
  }
  // Do not bubble a header/frame click to the covered source split.
  p_event->accept();
}

void LinkPreviewOverlay::raiseSurface() {
  if (!isVisible()) {
    return;
  }
  raise();
#ifdef Q_OS_WIN
  // QWidget sibling order can differ from the native order after WebEngine
  // creates or reparents its child windows. Raise both without activating.
  if (auto *handle = windowHandle()) {
    handle->raise();
  }
#endif
}

void LinkPreviewOverlay::scheduleRaise() {
  if (m_raisePending) {
    return;
  }
  m_raisePending = true;
  QTimer::singleShot(0, this, [this]() {
    m_raisePending = false;
    raiseSurface();
  });
}
