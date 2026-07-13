#include "resizestickerdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace vnotex;

ResizeStickerDialog::ResizeStickerDialog(int p_rowSpan, int p_colSpan, int p_maxRowSpan,
                                         int p_maxColSpan, QWidget *p_parent)
    : QDialog(p_parent) {
  setupUI(p_rowSpan, p_colSpan, p_maxRowSpan, p_maxColSpan);
}

void ResizeStickerDialog::setupUI(int p_rowSpan, int p_colSpan, int p_maxRowSpan,
                                  int p_maxColSpan) {
  setWindowTitle(tr("Resize Sticker"));

  auto *layout = new QVBoxLayout(this);

  auto *form = new QFormLayout();

  m_rowSpanBox = new QSpinBox(this);
  m_rowSpanBox->setObjectName(QStringLiteral("RowSpanBox"));
  m_rowSpanBox->setRange(1, qMax(1, p_maxRowSpan));
  m_rowSpanBox->setValue(p_rowSpan);
  form->addRow(tr("Row span:"), m_rowSpanBox);

  m_colSpanBox = new QSpinBox(this);
  m_colSpanBox->setObjectName(QStringLiteral("ColSpanBox"));
  m_colSpanBox->setRange(1, qMax(1, p_maxColSpan));
  m_colSpanBox->setValue(p_colSpan);
  form->addRow(tr("Column span:"), m_colSpanBox);

  layout->addLayout(form);

  auto *buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttonBox);
}

int ResizeStickerDialog::rowSpan() const {
  return m_rowSpanBox->value();
}

int ResizeStickerDialog::colSpan() const {
  return m_colSpanBox->value();
}
