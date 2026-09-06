#include "tableinsertpopup.h"

#include <QAction>
#include <QCursor>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

using namespace vnotex;

class TableInsertPopup::Grid : public QWidget {
public:
  Grid(TableInsertPopup *p_popup, QWidget *p_parent) : QWidget(p_parent), m_popup(p_popup) {
    setObjectName(QStringLiteral("tableInsertGrid"));
    setMouseTracking(true);
    setFixedSize(c_dimension * c_cellSize, c_dimension * c_cellSize);
  }

  int getHoveredBodyRows() const { return m_row + 1; }
  int getHoveredColumns() const { return m_column + 1; }

protected:
  bool event(QEvent *p_event) override {
    if (p_event->type() == QEvent::Enter) {
      updateHovered(mapFromGlobal(QCursor::pos()));
    }
    return QWidget::event(p_event);
  }

  void showEvent(QShowEvent *p_event) override {
    resetHovered();
    QWidget::showEvent(p_event);
  }

  void mouseMoveEvent(QMouseEvent *p_event) override { updateHovered(p_event->pos()); }

  void leaveEvent(QEvent *p_event) override {
    resetHovered();
    QWidget::leaveEvent(p_event);
  }

  void mousePressEvent(QMouseEvent *p_event) override {
    if (p_event->button() != Qt::LeftButton || !rect().contains(p_event->pos())) {
      p_event->ignore();
      return;
    }
    updateHovered(p_event->pos());
    const int bodyRows = getHoveredBodyRows();
    const int columns = getHoveredColumns();
    m_popup->hide();
    emit m_popup->tableSelected(bodyRows, columns);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const auto &colors = palette();
    painter.fillRect(rect(), colors.window());
    for (int row = 0; row < c_dimension; ++row) {
      for (int column = 0; column < c_dimension; ++column) {
        const QRect cell(column * c_cellSize + 2, row * c_cellSize + 2, c_cellSize - 4,
                         c_cellSize - 4);
        const bool highlighted = row <= m_row && column <= m_column;
        painter.fillRect(cell, highlighted ? colors.highlight() : colors.base());
        painter.setPen(colors.color(highlighted ? QPalette::Highlight : QPalette::Mid));
        painter.drawRect(cell.adjusted(0, 0, -1, -1));
      }
    }
  }

private:
  void updateHovered(const QPoint &p_pos) {
    const bool inside = rect().contains(p_pos);
    const int row = inside ? p_pos.y() / c_cellSize : -1;
    const int column = inside ? p_pos.x() / c_cellSize : -1;
    if (row != m_row || column != m_column) {
      m_row = row;
      m_column = column;
      update();
    }
  }

  void resetHovered() { updateHovered(QPoint(-1, -1)); }

  static constexpr int c_dimension = 7;
  static constexpr int c_cellSize = 24;
  TableInsertPopup *m_popup;
  int m_row = -1;
  int m_column = -1;
};

TableInsertPopup::TableInsertPopup(QToolButton *p_button, QWidget *p_parent)
    : ButtonPopup(p_button, p_parent) {
  auto *widget = new QWidget(this);
  auto *layout = new QVBoxLayout(widget);
  layout->addWidget(new QLabel(tr("Insert Table"), widget));
  m_grid = new Grid(this, widget);
  layout->addWidget(m_grid);

  auto *dialogButton = new QToolButton(widget);
  dialogButton->setObjectName(QStringLiteral("tableInsertDialogButton"));
  dialogButton->setText(tr("Insert Table..."));
  dialogButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  dialogButton->setAutoRaise(true);
  dialogButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto refreshIcon = [this, dialogButton]() {
    if (m_button) {
      auto *action = m_button->defaultAction();
      dialogButton->setIcon(action ? action->icon() : m_button->icon());
    }
  };
  refreshIcon();
  connect(this, &QMenu::aboutToShow, this, refreshIcon);
  connect(dialogButton, &QToolButton::clicked, this, [this]() {
    hide();
    emit dialogRequested();
  });
  layout->addWidget(dialogButton);
  addWidget(widget);
}

int TableInsertPopup::getHoveredBodyRows() const { return m_grid->getHoveredBodyRows(); }

int TableInsertPopup::getHoveredColumns() const { return m_grid->getHoveredColumns(); }
