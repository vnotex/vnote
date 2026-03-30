#include "attachmentdragdropareaindicator2.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>

#include <core/services/buffer2.h>
#include <utils/urldragdroputils.h>

using namespace vnotex;

AttachmentDragDropAreaIndicator2::AttachmentDragDropAreaIndicator2(QWidget *p_parent)
    : QFrame(p_parent) {
  setupUI();
  setAcceptDrops(true);
  hide();
}

void AttachmentDragDropAreaIndicator2::setupUI() {
  auto *mainLayout = new QHBoxLayout(this);

  auto *label = new QLabel(tr("Drag And Drop Files To Attach"), this);
  label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  mainLayout->addWidget(label);
}

void AttachmentDragDropAreaIndicator2::setBuffer(Buffer2 *p_buffer) {
  m_buffer = p_buffer;
}

bool AttachmentDragDropAreaIndicator2::isAccepted(const QDragEnterEvent *p_event) {
  return p_event->mimeData()->hasFormat(QStringLiteral("text/uri-list"));
}

void AttachmentDragDropAreaIndicator2::dragEnterEvent(QDragEnterEvent *p_event) {
  if (isAccepted(p_event)) {
    p_event->acceptProposedAction();
    return;
  }
  QFrame::dragEnterEvent(p_event);
}

void AttachmentDragDropAreaIndicator2::dropEvent(QDropEvent *p_event) {
  if (!m_buffer) {
    QFrame::dropEvent(p_event);
    return;
  }

  bool handled = UrlDragDropUtils::handleDropEvent(p_event, [this](const QStringList &p_files) {
    int count = 0;
    for (const auto &filePath : p_files) {
      QString result = m_buffer->insertAttachment(filePath);
      if (!result.isEmpty()) {
        ++count;
      }
    }
    if (count > 0) {
      emit filesAttached(count);
    }
  });

  if (handled) {
    hide();
    return;
  }

  QFrame::dropEvent(p_event);
}

void AttachmentDragDropAreaIndicator2::mouseReleaseEvent(QMouseEvent *p_event) {
  QFrame::mouseReleaseEvent(p_event);
  hide();
}
