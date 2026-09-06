#ifndef TABLEINSERTPOPUP_H
#define TABLEINSERTPOPUP_H

#include "buttonpopup.h"

namespace vnotex {
class TableInsertPopup : public ButtonPopup {
  Q_OBJECT
public:
  TableInsertPopup(QToolButton *p_button, QWidget *p_parent = nullptr);

  // One-based body-row/column preview, or zero when no cell is hovered.
  int getHoveredBodyRows() const;
  int getHoveredColumns() const;

signals:
  void tableSelected(int p_bodyRows, int p_columns);
  void dialogRequested();

private:
  class Grid;
  Grid *m_grid = nullptr;
};
} // namespace vnotex

#endif // TABLEINSERTPOPUP_H
