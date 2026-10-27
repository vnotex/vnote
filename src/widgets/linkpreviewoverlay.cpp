#include "linkpreviewoverlay.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include "propertydefs.h"
#include "viewsplit2.h"
#include "viewwindow2.h"

using namespace vnotex;

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

  auto *closeButton = new QToolButton(header);
  closeButton->setObjectName(QStringLiteral("closeLinkPreviewButton"));
  closeButton->setProperty(PropertyDefs::c_actionToolButton, true);
  m_closeAction = new QAction(style()->standardIcon(QStyle::SP_TitleBarCloseButton),
                              tr("Close Preview"), closeButton);
  m_closeAction->setObjectName(QStringLiteral("closeLinkPreviewAction"));
  closeButton->setDefaultAction(m_closeAction);
  connect(m_closeAction, &QAction::triggered, this, &LinkPreviewOverlay::closeRequested);
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
    raiseSurface();
    scheduleRaise();
    emit visibilityChanged(true);
    break;
  case QEvent::Hide:
    emit visibilityChanged(false);
    break;
  case QEvent::StyleChange:
  case QEvent::PaletteChange:
  case QEvent::FontChange:
    if (m_closeAction) {
      m_closeAction->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    }
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
