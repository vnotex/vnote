#include "imagesizedialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

namespace {
// Tests find widgets by object name, never by label text.
const char *kWidthEditName = "imageSizeWidthEdit";
const char *kHeightEditName = "imageSizeHeightEdit";
const char *kScaleSliderName = "imageSizeScaleSlider";
constexpr int c_maxImageDimension = 999999;

int positiveIntOrZero(const QLineEdit *p_edit) {
  bool ok = false;
  const int value = p_edit->text().trimmed().toInt(&ok);
  return (ok && value > 0) ? value : 0;
}
} // namespace

ImageSizeDialog::ImageSizeDialog(const QString &p_title, int p_width, int p_height,
                                 const QSize &p_imageSize, QWidget *p_parent)
    : Dialog(p_parent), m_imageSize(p_imageSize) {
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

  m_scaleSlider = new QSlider(Qt::Horizontal, mainWidget);
  m_scaleSlider->setObjectName(QLatin1String(kScaleSliderName));
  m_scaleSlider->setAccessibleName(tr("Scale (%)"));
  m_scaleSlider->setPageStep(10);
  m_scaleSlider->setMinimumWidth(120);
  auto scaleLabel = new QLabel(tr("Scale (%)"), mainWidget);
  scaleLabel->setBuddy(m_scaleSlider);
  gridLayout->addWidget(scaleLabel, 2, 0);
  auto scaleLayout = new QHBoxLayout();
  scaleLayout->addWidget(m_scaleSlider, 1);
  m_scaleValueLabel = new QLabel(mainWidget);
  m_scaleValueLabel->setMinimumWidth(
      m_scaleValueLabel->fontMetrics().horizontalAdvance(QStringLiteral("200%")));
  m_scaleValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  scaleLayout->addWidget(m_scaleValueLabel);
  gridLayout->addLayout(scaleLayout, 2, 1);

  connect(m_scaleSlider, &QSlider::valueChanged, this, [this](int p_percent) {
    // Always scale the baseline, not the rounded result of the previous movement.
    const qreal ratio = p_percent / 100.0;
    m_widthEdit->setText(QString::number(qMax(1, qRound(m_scaleBaseSize.width() * ratio))));
    m_heightEdit->setText(QString::number(qMax(1, qRound(m_scaleBaseSize.height() * ratio))));
    m_scaleValueLabel->setText(QString::number(p_percent) + QLatin1Char('%'));
  });
  // Slider writes do not emit textEdited; only manual edits establish a new baseline.
  connect(m_widthEdit, &QLineEdit::textEdited, this, &ImageSizeDialog::resetScaleBase);
  connect(m_heightEdit, &QLineEdit::textEdited, this, &ImageSizeDialog::resetScaleBase);
  resetScaleBase();

  auto hint = new QLabel(tr("Leave both empty to remove the size."), mainWidget);
  hint->setWordWrap(true);
  gridLayout->addWidget(hint, 3, 0, 1, 2);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
}

void ImageSizeDialog::resetScaleBase() {
  qreal width = getImageWidth();
  qreal height = getImageHeight();
  if (!m_imageSize.isEmpty()) {
    if (width > 0 && height <= 0) {
      height = width * m_imageSize.height() / m_imageSize.width();
    } else if (height > 0 && width <= 0) {
      width = height * m_imageSize.width() / m_imageSize.height();
    } else if (width <= 0 && height <= 0) {
      width = m_imageSize.width();
      height = m_imageSize.height();
    }
  }
  m_scaleBaseSize = QSizeF(width, height);
  const bool available =
      width > 0 && height > 0 && width <= c_maxImageDimension && height <= c_maxImageDimension;
  const QSignalBlocker blocker(m_scaleSlider);
  // A common upper bound preserves the ratio without clipping one axis independently.
  const int maximum =
      available ? qMin(200, int(c_maxImageDimension * 100.0 / qMax(width, height))) : 200;
  m_scaleSlider->setRange(1, maximum);
  m_scaleSlider->setValue(100);
  m_scaleSlider->setEnabled(available);
  m_scaleSlider->setToolTip(available
                                ? tr("Scale both dimensions proportionally from the current size")
                                : tr("Enter a width and height to enable scaling"));
  m_scaleValueLabel->setText(available ? QStringLiteral("100%") : QStringLiteral("—"));
}

void ImageSizeDialog::showEvent(QShowEvent *p_event) {
  Dialog::showEvent(p_event);

  m_widthEdit->selectAll();
  m_widthEdit->setFocus();
}

int ImageSizeDialog::getImageWidth() const { return positiveIntOrZero(m_widthEdit); }

int ImageSizeDialog::getImageHeight() const { return positiveIntOrZero(m_heightEdit); }
