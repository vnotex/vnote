#include "marknodedialog2.h"

#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using namespace vnotex;

namespace {

// 12 preset colors shared by both text and background sections.
const QStringList c_presetColors = {
    QStringLiteral("#e53935"), // Red
    QStringLiteral("#d81b60"), // Pink
    QStringLiteral("#8e24aa"), // Purple
    QStringLiteral("#1e88e5"), // Blue
    QStringLiteral("#00acc1"), // Cyan
    QStringLiteral("#00897b"), // Teal
    QStringLiteral("#43a047"), // Green
    QStringLiteral("#7cb342"), // Lime
    QStringLiteral("#fdd835"), // Yellow
    QStringLiteral("#fb8c00"), // Orange
    QStringLiteral("#6d4c41"), // Brown
    QStringLiteral("#757575"), // Grey
};

const int c_swatchSize = 24;
const int c_gridColumns = 6;

QPixmap colorPixmap(const QColor &p_color, int p_size) {
  QPixmap pix(p_size, p_size);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(p_color);
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(1, 1, p_size - 2, p_size - 2, 3, 3);
  return pix;
}

} // anonymous namespace

MarkNodeDialog2::MarkNodeDialog2(const QString &p_currentTextColor, const QString &p_currentBgColor,
                                 QWidget *p_parent)
    : ScrollDialog(p_parent), m_textColor(p_currentTextColor), m_bgColor(p_currentBgColor) {
  setupUI();
}

MarkNodeDialog2::~MarkNodeDialog2() = default;

void MarkNodeDialog2::setupUI() {
  auto *mainWidget = new QWidget(this);
  auto *layout = new QVBoxLayout(mainWidget);

  // Text color section.
  auto *textSection = buildColorSection(tr("Text Color"), m_textColor, m_textSwatches,
                                        c_swatchCount, m_textColorIndicator, m_textColor);
  layout->addWidget(textSection);

  // Background color section.
  auto *bgSection = buildColorSection(tr("Background Color"), m_bgColor, m_bgSwatches,
                                      c_swatchCount, m_bgColorIndicator, m_bgColor);
  layout->addWidget(bgSection);

  // Preview section.
  auto *previewGroup = new QLabel(tr("Preview"), mainWidget);
  layout->addWidget(previewGroup);

  m_previewLabel = new QLabel(tr("Sample Text"), mainWidget);
  m_previewLabel->setAlignment(Qt::AlignCenter);
  m_previewLabel->setMinimumHeight(48);
  m_previewLabel->setAutoFillBackground(true);
  m_previewLabel->setFrameShape(QFrame::StyledPanel);

  QFont previewFont = m_previewLabel->font();
  previewFont.setPointSize(previewFont.pointSize() + 2);
  m_previewLabel->setFont(previewFont);

  layout->addWidget(m_previewLabel);

  layout->addStretch();

  setCentralWidget(mainWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("Mark Node"));

  updatePreview();
}

QWidget *MarkNodeDialog2::buildColorSection(const QString &p_title, const QString &p_currentColor,
                                            QToolButton **p_swatchButtons, int p_swatchCount,
                                            QLabel *&p_selectedIndicator, QString &p_colorStorage) {
  auto *section = new QWidget();
  auto *sectionLayout = new QVBoxLayout(section);
  sectionLayout->setContentsMargins(0, 0, 0, 0);

  // Title label.
  auto *titleLabel = new QLabel(p_title, section);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  sectionLayout->addWidget(titleLabel);

  // Swatch grid.
  auto *gridWidget = new QWidget(section);
  auto *grid = new QGridLayout(gridWidget);
  grid->setSpacing(4);
  grid->setContentsMargins(0, 0, 0, 0);

  for (int i = 0; i < p_swatchCount && i < c_presetColors.size(); ++i) {
    auto *btn = createSwatchButton(c_presetColors[i], gridWidget);
    p_swatchButtons[i] = btn;

    int row = i / c_gridColumns;
    int col = i % c_gridColumns;
    grid->addWidget(btn, row, col);

    connect(btn, &QToolButton::clicked, this,
            [this, i, p_swatchButtons, p_swatchCount, &p_selectedIndicator, &p_colorStorage]() {
              onSwatchClicked(c_presetColors[i], p_swatchButtons, p_swatchCount,
                              p_selectedIndicator, p_colorStorage);
            });
  }

  sectionLayout->addWidget(gridWidget);

  // Buttons row: selected indicator + Custom + Clear.
  auto *btnRow = new QWidget(section);
  auto *btnLayout = new QHBoxLayout(btnRow);
  btnLayout->setContentsMargins(0, 0, 0, 0);

  // Selected color indicator.
  p_selectedIndicator = new QLabel(btnRow);
  p_selectedIndicator->setFixedSize(c_swatchSize, c_swatchSize);
  p_selectedIndicator->setFrameShape(QFrame::StyledPanel);
  if (!p_currentColor.isEmpty()) {
    p_selectedIndicator->setPixmap(colorPixmap(QColor(p_currentColor), c_swatchSize));
    p_selectedIndicator->setToolTip(p_currentColor);
  } else {
    p_selectedIndicator->setToolTip(tr("None"));
  }
  btnLayout->addWidget(p_selectedIndicator);

  auto *customBtn = new QPushButton(tr("Custom..."), btnRow);
  connect(customBtn, &QPushButton::clicked, this,
          [this, p_swatchButtons, p_swatchCount, &p_selectedIndicator, &p_colorStorage]() {
            onCustomColor(p_swatchButtons, p_swatchCount, p_selectedIndicator, p_colorStorage);
          });
  btnLayout->addWidget(customBtn);

  auto *clearBtn = new QPushButton(tr("Clear"), btnRow);
  connect(clearBtn, &QPushButton::clicked, this,
          [this, p_swatchButtons, p_swatchCount, &p_selectedIndicator, &p_colorStorage]() {
            onClearColor(p_swatchButtons, p_swatchCount, p_selectedIndicator, p_colorStorage);
          });
  btnLayout->addWidget(clearBtn);

  btnLayout->addStretch();

  sectionLayout->addWidget(btnRow);

  // Highlight the matching swatch if current color matches a preset.
  updateSwatchHighlight(p_swatchButtons, p_swatchCount, p_currentColor);

  return section;
}

QToolButton *MarkNodeDialog2::createSwatchButton(const QString &p_color, QWidget *p_parent) {
  auto *btn = new QToolButton(p_parent);
  btn->setFixedSize(c_swatchSize, c_swatchSize);
  btn->setIcon(QIcon(colorPixmap(QColor(p_color), c_swatchSize)));
  btn->setIconSize(QSize(c_swatchSize - 4, c_swatchSize - 4));
  btn->setToolTip(p_color);
  btn->setCheckable(true);
  btn->setAutoExclusive(false);
  return btn;
}

void MarkNodeDialog2::updateSwatchHighlight(QToolButton **p_swatchButtons, int p_swatchCount,
                                            const QString &p_activeColor) {
  for (int i = 0; i < p_swatchCount; ++i) {
    bool match = !p_activeColor.isEmpty() && i < c_presetColors.size() &&
                 QColor(p_activeColor) == QColor(c_presetColors[i]);
    p_swatchButtons[i]->setChecked(match);
  }
}

void MarkNodeDialog2::updatePreview() {
  const QColor bg = !m_bgColor.isEmpty() ? QColor(m_bgColor) : palette().color(QPalette::Base);
  const QColor fg = !m_textColor.isEmpty() ? QColor(m_textColor) : palette().color(QPalette::Text);

  m_previewLabel->setStyleSheet(QStringLiteral("color: %1; background-color: %2;")
                                    .arg(fg.name(QColor::HexRgb), bg.name(QColor::HexRgb)));
}

void MarkNodeDialog2::onSwatchClicked(const QString &p_color, QToolButton **p_swatchButtons,
                                      int p_swatchCount, QLabel *p_indicator,
                                      QString &p_colorStorage) {
  p_colorStorage = p_color;
  p_indicator->setPixmap(colorPixmap(QColor(p_color), c_swatchSize));
  p_indicator->setToolTip(p_color);
  updateSwatchHighlight(p_swatchButtons, p_swatchCount, p_color);
  updatePreview();
}

void MarkNodeDialog2::onCustomColor(QToolButton **p_swatchButtons, int p_swatchCount,
                                    QLabel *p_indicator, QString &p_colorStorage) {
  QColor initial = p_colorStorage.isEmpty() ? Qt::black : QColor(p_colorStorage);
  QColor chosen = QColorDialog::getColor(initial, this, tr("Select Color"));
  if (chosen.isValid()) {
    p_colorStorage = chosen.name();
    p_indicator->setPixmap(colorPixmap(chosen, c_swatchSize));
    p_indicator->setToolTip(chosen.name());
    updateSwatchHighlight(p_swatchButtons, p_swatchCount, chosen.name());
    updatePreview();
  }
}

void MarkNodeDialog2::onClearColor(QToolButton **p_swatchButtons, int p_swatchCount,
                                   QLabel *p_indicator, QString &p_colorStorage) {
  p_colorStorage.clear();
  p_indicator->setPixmap(QPixmap());
  p_indicator->setToolTip(tr("None"));
  updateSwatchHighlight(p_swatchButtons, p_swatchCount, QString());
  updatePreview();
}

void MarkNodeDialog2::acceptedButtonClicked() { accept(); }

QString MarkNodeDialog2::textColor() const { return m_textColor; }

QString MarkNodeDialog2::backgroundColor() const { return m_bgColor; }
