#ifndef RESIZESTICKERDIALOG_H
#define RESIZESTICKERDIALOG_H

#include <QDialog>

class QSpinBox;

namespace vnotex {

// Dedicated dialog to resize a sticker on the dashboard board.
//
// Collects the new row span and column span in a single form (the sticker's
// position is left unchanged; moving is handled by the Move menu actions).
class ResizeStickerDialog : public QDialog {
  Q_OBJECT
public:
  ResizeStickerDialog(int p_rowSpan, int p_colSpan, int p_maxRowSpan, int p_maxColSpan,
                      QWidget *p_parent = nullptr);

  int rowSpan() const;
  int colSpan() const;

private:
  void setupUI(int p_rowSpan, int p_colSpan, int p_maxRowSpan, int p_maxColSpan);

  QSpinBox *m_rowSpanBox = nullptr;
  QSpinBox *m_colSpanBox = nullptr;
};

} // namespace vnotex

#endif // RESIZESTICKERDIALOG_H
