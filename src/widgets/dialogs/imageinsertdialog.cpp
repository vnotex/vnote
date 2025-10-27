#include "imageinsertdialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <vtextedit/markdownutils.h>
#include <vtextedit/networkutils.h>

#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

int ImageInsertDialog::s_lastScaleSliderValue = 10;

int ImageInsertDialog::s_lastScaleWidth = -1;

bool ImageInsertDialog::s_fixedScaleWidth = false;

ImageInsertDialog::ImageInsertDialog(const QString &p_title, const QString &p_imageTitle,
                                     const QString &p_imageAlt, const QString &p_imagePath,
                                     bool p_browserEnabled, QWidget *p_parent)
    : Dialog(p_parent), m_browserEnabled(p_browserEnabled) {
  m_imagePathCheckTimer = new QTimer(this);
  m_imagePathCheckTimer->setSingleShot(true);
  m_imagePathCheckTimer->setInterval(500);
  connect(m_imagePathCheckTimer, &QTimer::timeout, this, &ImageInsertDialog::checkImagePathInput);

  setupUI(p_title, p_imageTitle, p_imageAlt, p_imagePath);

  checkInput();
}

void ImageInsertDialog::setupUI(const QString &p_title, const QString &p_imageTitle,
                                const QString &p_imageAlt, const QString &p_imagePath) {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = new QVBoxLayout(mainWidget);

  auto gridLayout = new QGridLayout();
  mainLayout->addLayout(gridLayout);

  mainLayout->addStretch();

  // Image Path.
  m_imagePathEdit = WidgetsFactory::createLineEdit(p_imagePath, mainWidget);
  m_imagePathEdit->setReadOnly(!m_browserEnabled);
  gridLayout->addWidget(new QLabel(tr("From:"), mainWidget), 0, 0, 1, 1);
  gridLayout->addWidget(m_imagePathEdit, 0, 1, 1, 3);
  connect(m_imagePathEdit, &QLineEdit::textChanged, this,
          [this]() { m_imagePathCheckTimer->start(); });

  m_browseBtn = new QPushButton(tr("&Browse"), mainWidget);
  m_browseBtn->setEnabled(m_browserEnabled);
  gridLayout->addWidget(m_browseBtn, 0, 4, 1, 1);
  connect(m_browseBtn, &QPushButton::clicked, this, &ImageInsertDialog::browseFile);

  // Image Title.
  m_imageTitleEdit = WidgetsFactory::createLineEdit(p_imageTitle, mainWidget);
  auto titleValidator = new QRegularExpressionValidator(
      QRegularExpression(vte::MarkdownUtils::c_imageTitleRegExp), m_imageTitleEdit);
  m_imageTitleEdit->setValidator(titleValidator);
  gridLayout->addWidget(new QLabel(tr("Title:"), mainWidget), 1, 0, 1, 1);
  gridLayout->addWidget(m_imageTitleEdit, 1, 1, 1, 3);
  connect(m_imageTitleEdit, &QLineEdit::textChanged, this, &ImageInsertDialog::checkInput);

  // Image Alt.
  m_imageAltEdit = WidgetsFactory::createLineEdit(p_imageAlt, mainWidget);
  auto altValidator = new QRegularExpressionValidator(
      QRegularExpression(vte::MarkdownUtils::c_imageAltRegExp), m_imageAltEdit);
  m_imageAltEdit->setValidator(altValidator);
  gridLayout->addWidget(new QLabel(tr("Alt text:"), mainWidget), 2, 0, 1, 1);
  gridLayout->addWidget(m_imageAltEdit, 2, 1, 1, 3);

  // Scale.
  m_widthSpin = WidgetsFactory::createSpinBox(mainWidget);
  m_widthSpin->setMinimum(1);
  m_widthSpin->setSingleStep(10);
  m_widthSpin->setSuffix(" px");
  connect(m_widthSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
          [this](int p_val) {
            if (m_image.isNull()) {
              return;
            }

            int height = m_image.height() * (1.0 * p_val / m_image.width());
            m_imageLabel->resize(p_val, height);

            s_lastScaleWidth = p_val;
          });
  // 0.1 to 2.0 -> 1 to 20.
  m_scaleSlider = new QSlider(mainWidget);
  m_scaleSlider->setOrientation(Qt::Horizontal);
  m_scaleSlider->setMinimum(1);
  m_scaleSlider->setMaximum(50);
  m_scaleSlider->setValue(s_lastScaleSliderValue);
  m_scaleSlider->setSingleStep(1);
  m_scaleSlider->setPageStep(5);
  connect(m_scaleSlider, &QSlider::valueChanged, this,
          &ImageInsertDialog::handleScaleSliderValueChanged);
  m_sliderLabel = new QLabel("1x", mainWidget);
  gridLayout->addWidget(new QLabel(tr("Scaling width:"), mainWidget), 3, 0, 1, 1);
  gridLayout->addWidget(m_widthSpin, 3, 1, 1, 1);
  gridLayout->addWidget(m_scaleSlider, 3, 2, 1, 2);
  gridLayout->addWidget(m_sliderLabel, 3, 4, 1, 1);

  {
    auto fixedWidthCheckBox = WidgetsFactory::createCheckBox(tr("Fixed scaling width"), mainWidget);
    fixedWidthCheckBox->setChecked(s_fixedScaleWidth);
    connect(fixedWidthCheckBox, &QCheckBox::stateChanged, this,
            [](int p_state) { s_fixedScaleWidth = p_state == Qt::Checked; });
    gridLayout->addWidget(fixedWidthCheckBox, 4, 1, 1, 1);
  }

  // Preview area.
  m_imageLabel = new QLabel(mainWidget);
  m_imageLabel->setScaledContents(true);
  m_previewArea = new QScrollArea(mainWidget);
  m_previewArea->setBackgroundRole(QPalette::Dark);
  m_previewArea->setWidget(m_imageLabel);
  m_previewArea->setMinimumSize(256, 256);
  gridLayout->addWidget(m_previewArea, 5, 0, 1, 5);

  setImageControlsVisible(false);

  gridLayout->setColumnStretch(0, 0);
  gridLayout->setColumnStretch(1, 0);
  gridLayout->setColumnStretch(2, 1);
  gridLayout->setColumnStretch(3, 1);
  gridLayout->setColumnStretch(4, 0);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(p_title);
}

void ImageInsertDialog::setImageControlsVisible(bool p_visible) {
  m_widthSpin->setEnabled(p_visible);
  m_scaleSlider->setEnabled(p_visible);
  m_sliderLabel->setEnabled(p_visible);

  m_previewArea->setVisible(p_visible);
}

void ImageInsertDialog::showEvent(QShowEvent *p_event) {
  Dialog::showEvent(p_event);

  m_imageTitleEdit->selectAll();
  m_imageTitleEdit->setFocus();
}

void ImageInsertDialog::checkImagePathInput() {
  const QString text = m_imagePathEdit->text();
  QUrl url = QUrl::fromUserInput(text);
  if (text.isEmpty() || !url.isValid()) {
    setImage(QImage());
    return;
  }

  if (url.isLocalFile()) {
    const auto localFile = url.toLocalFile();
    if (QFileInfo::exists(localFile)) {
      setImage(FileUtils::imageFromFile(localFile));
    } else {
      setImage(QImage());
    }

    m_source = Source::LocalFile;
  } else {
    setImage(QImage());
    m_source = Source::ImageData;

    if (!m_downloader) {
      m_downloader = new vte::NetworkAccess(this);
      connect(m_downloader, &vte::NetworkAccess::requestFinished, this,
              &ImageInsertDialog::handleImageDownloaded);
    }

    m_downloader->requestAsync(url);
  }

  m_imageTitleEdit->setText(QFileInfo(text).baseName());

  checkInput();
}

void ImageInsertDialog::checkInput() { setButtonEnabled(QDialogButtonBox::Ok, !m_image.isNull()); }

void ImageInsertDialog::browseFile() {
  auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
  QString filePath = QFileDialog::getOpenFileName(
      this, tr("Select Image To Insert"), sessionConfig.getExternalMediaDefaultPath(),
      tr("Images (*.png *.xpm *.jpg *.bmp *.gif *.svg *.webp);;All (*.*)"));
  if (filePath.isEmpty()) {
    return;
  }

  sessionConfig.setExternalMediaDefaultPath(PathUtils::parentDirPath(filePath));

  m_source = Source::LocalFile;

  setImagePath(filePath);

  m_imageTitleEdit->selectAll();
  m_imageTitleEdit->setFocus();
}

QString ImageInsertDialog::getImageTitle() const { return m_imageTitleEdit->text(); }

QString ImageInsertDialog::getImageAltText() const { return m_imageAltEdit->text(); }

QString ImageInsertDialog::getImagePath() const {
  if (m_tempFile.isNull()) {
    return m_imagePathEdit->text();
  } else {
    return m_tempFile->fileName();
  }
}

ImageInsertDialog::Source ImageInsertDialog::getImageSource() const { return m_source; }

void ImageInsertDialog::setImageSource(ImageInsertDialog::Source p_source) { m_source = p_source; }

const QImage &ImageInsertDialog::getImage() const { return m_image; }

void ImageInsertDialog::setImage(const QImage &p_image) {
  m_image = p_image;
  if (m_image.isNull()) {
    m_imageLabel->clear();
    setImageControlsVisible(false);
  } else {
    m_imageLabel->setPixmap(QPixmap::fromImage(m_image));

    m_imageLabel->adjustSize();

    m_widthSpin->setMaximum(m_image.width() * 5);

    if (s_fixedScaleWidth) {
      m_widthSpin->setValue(s_lastScaleWidth);
    } else {
      // Set the scaling widgets.
      if (m_scaleSlider->value() == s_lastScaleSliderValue) {
        // Trigger it manually.
        handleScaleSliderValueChanged(s_lastScaleSliderValue);
      } else {
        m_scaleSlider->setValue(s_lastScaleSliderValue);
      }
    }

    setImageControlsVisible(true);
  }

  checkInput();
}

void ImageInsertDialog::setImagePath(const QString &p_path) {
  m_tempFile.reset();
  m_imagePathEdit->setText(p_path);
}

int ImageInsertDialog::getScaledWidth() const {
  if (m_image.isNull()) {
    return 0;
  }

  int val = m_widthSpin->value();
  return val == m_image.width() ? 0 : val;
}

void ImageInsertDialog::handleImageDownloaded(const vte::NetworkReply &p_data,
                                              const QString &p_url) {
  setImage(QImage::fromData(p_data.m_data));

  // Save it to a temp file to avoid potential data loss via QImage.
  bool savedToFile = false;
  if (!p_data.m_data.isEmpty()) {
    auto format = QFileInfo(PathUtils::removeUrlParameters(p_url)).suffix();
    m_tempFile.reset(FileUtils::createTemporaryFile(format));
    if (m_tempFile->open()) {
      savedToFile = -1 != m_tempFile->write(p_data.m_data);
      m_tempFile->close();
    }
  }

  m_source = savedToFile ? Source::LocalFile : Source::ImageData;
  if (!savedToFile) {
    m_tempFile.reset();
  }
}

void ImageInsertDialog::handleScaleSliderValueChanged(int p_val) {
  if (m_image.isNull()) {
    return;
  }

  int width = m_image.width();
  qreal factor = 1.0;
  if (p_val != 10) {
    factor = p_val / 10.0;
    width = m_image.width() * factor;
  }

  m_widthSpin->setValue(width);
  m_sliderLabel->setText(QString::number(factor) + "x");

  s_lastScaleSliderValue = p_val;
}
