#include "viewareahomewidget.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace vnotex;

ViewAreaHomeWidget::ViewAreaHomeWidget(QWidget *p_parent)
    : QWidget(p_parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setAlignment(Qt::AlignCenter);

  auto *label = new QLabel(tr("<h2>Read, Write, and Think</h2>"), this);
  label->setAlignment(Qt::AlignCenter);
  layout->addWidget(label);
}
