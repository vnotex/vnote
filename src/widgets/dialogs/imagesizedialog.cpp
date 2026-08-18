#include "imagesizedialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

namespace {
// Tests find widgets by object name, never by label text.
const char *kWidthEditName = "imageSizeWidthEdit";
const char *kHeightEditName = "imageSizeHeightEdit";

int positiveIntOrZero(const QLineEdit *p_edit) {
  bool ok = false;
  const int value = p_edit->text().trimmed().toInt(&ok);
  return (ok && value > 0) ? value : 0;
}
} // namespace

ImageSizeDialog::ImageSizeDialog(const QString &p_title, int p_width, int p_height,
                                 QWidget *p_parent)
    : Dialog(p_parent) {
  setupUI(p_width, p_height);
  setWindowTitle(p_title);
}

void ImageSizeDialog::setupUI(int p_width, int p_height) {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = new QVBoxLayout(mainWidget);
  auto gridLayout = new QGridLayout();
  mainLayout->addLayout(gridLayout);
  mainLayout->addStretch();

  auto sizeValidator =
      new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), mainWidget);

  m_widthEdit = WidgetsFactory::createLineEdit(p_width > 0 ? QString::number(p_width) : QString(),
                                               mainWidget);
  m_widthEdit->setObjectName(QLatin1String(kWidthEditName));
  m_widthEdit->setValidator(sizeValidator);
  gridLayout->addWidget(new QLabel(tr("Width (px)"), mainWidget), 0, 0, 1, 1);
  gridLayout->addWidget(m_widthEdit, 0, 1, 1, 1);

  m_heightEdit = WidgetsFactory::createLineEdit(
      p_height > 0 ? QString::number(p_height) : QString(), mainWidget);
  m_heightEdit->setObjectName(QLatin1String(kHeightEditName));
  m_heightEdit->setValidator(sizeValidator);
  gridLayout->addWidget(new QLabel(tr("Height (px)"), mainWidget), 1, 0, 1, 1);
  gridLayout->addWidget(m_heightEdit, 1, 1, 1, 1);

  auto hint = new QLabel(tr("Leave both empty to remove the size."), mainWidget);
  hint->setWordWrap(true);
  gridLayout->addWidget(hint, 2, 0, 1, 2);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
}

void ImageSizeDialog::showEvent(QShowEvent *p_event) {
  Dialog::showEvent(p_event);

  m_widthEdit->selectAll();
  m_widthEdit->setFocus();
}

int ImageSizeDialog::getImageWidth() const { return positiveIntOrZero(m_widthEdit); }

int ImageSizeDialog::getImageHeight() const { return positiveIntOrZero(m_heightEdit); }
