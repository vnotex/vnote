#ifndef STICKERDROPINDICATOR_H
#define STICKERDROPINDICATOR_H

#include <QColor>
#include <QWidget>

namespace vnotex {

// Ghost rectangle showing where a dragged sticker would land.
//
// Pure leaf presentational widget (no ServiceLocator): the board positions it
// over the candidate cell region, flags it valid/invalid, and injects both
// colors. Transparent for mouse events so it never interferes with the drag.
class StickerDropIndicator : public QWidget {
  Q_OBJECT
public:
  explicit StickerDropIndicator(QWidget *p_parent = nullptr);

  // Colors for a committable (accent) vs rejected (invalid) drop target.
  void setColors(const QColor &p_accent, const QColor &p_invalid);

  void setValidity(bool p_valid);
  bool isValid() const { return m_valid; }

protected:
  void paintEvent(QPaintEvent *p_event) override;

private:
  QColor m_accent;
  QColor m_invalid;
  bool m_valid = true;
};

} // namespace vnotex

#endif // STICKERDROPINDICATOR_H
