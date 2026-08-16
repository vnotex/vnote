#include "stickerdropindicator.h"

#include <QPainter>

using namespace vnotex;

namespace {
// Alpha of the translucent fill under the ghost's border.
constexpr int kGhostFillAlpha = 48;
} // namespace

StickerDropIndicator::StickerDropIndicator(QWidget *p_parent) : QWidget(p_parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void StickerDropIndicator::setColors(const QColor &p_accent, const QColor &p_invalid) {
  m_accent = p_accent;
  m_invalid = p_invalid;
  update();
}

void StickerDropIndicator::setValidity(bool p_valid) {
  if (m_valid == p_valid) {
    return;
  }
  m_valid = p_valid;
  update();
}

void StickerDropIndicator::paintEvent(QPaintEvent *p_event) {
  Q_UNUSED(p_event)
  const QColor base = m_valid ? m_accent : m_invalid;
  if (!base.isValid()) {
    return;
  }

  const QRect box = rect().adjusted(1, 1, -2, -2);
  if (box.width() <= 0 || box.height() <= 0) {
    return;
  }

  QPainter painter(this);
  QColor fill = base;
  fill.setAlpha(kGhostFillAlpha);
  painter.fillRect(box, fill);

  QPen pen(base);
  pen.setWidth(2);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(box);
}
