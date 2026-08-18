#include "imageinsertdialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <net/networkutils.h>
#include <vtextedit/markdownutils.h>

#include <core/configmgr2.h>
#include <core/sessionconfig.h>
#include <gui/utils/imageutils.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

namespace {
// Tests find widgets by object name, never by label text.
const char *kImageWidthEditName = "imageWidthEdit";
const char *kImageHeightEditName = "imageHeightEdit";

int positiveIntOrZero(const QLineEdit *p_edit) {
  bool ok = false;
  const int value = p_edit->text().trimmed().toInt(&ok);
  return (ok && value > 0) ? value : 0;
}
} // namespace

ImageInsertDialog::ImageInsertDialog(const QString &p_title, const QString &p_imageTitle,
                                     const QString &p_imageAlt, const QString &p_imagePath,
                                     ConfigMgr2 *p_configMgr, bool p_browserEnabled,
                                     QWidget *p_parent)
    : Dialog(p_parent), m_browserEnabled(p_browserEnabled), m_configMgr(p_configMgr) {
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
  gridLayout->addWidget(new QLabel(tr("From"), mainWidget), 0, 0, 1, 1);
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
      QRegularExpression(QStringLiteral("[^\\[\\]]*")), m_imageTitleEdit);
  m_imageTitleEdit->setValidator(titleValidator);
  gridLayout->addWidget(new QLabel(tr("Title"), mainWidget), 1, 0, 1, 1);
  gridLayout->addWidget(m_imageTitleEdit, 1, 1, 1, 3);
  connect(m_imageTitleEdit, &QLineEdit::textChanged, this, &ImageInsertDialog::checkInput);

  // Image Alt.
  m_imageAltEdit = WidgetsFactory::createLineEdit(p_imageAlt, mainWidget);
  auto altValidator = new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("[^\"'()]*")), m_imageAltEdit);
  m_imageAltEdit->setValidator(altValidator);
  gridLayout->addWidget(new QLabel(tr("Alt text"), mainWidget), 2, 0, 1, 1);
  gridLayout->addWidget(m_imageAltEdit, 2, 1, 1, 3);

  // Optional size. Left EMPTY by default on purpose: pre-filling the source
  // image's natural size would turn every single insert into an HTML `<img>`.
  // The natural size is offered as a placeholder instead.
  auto sizeValidator =
      new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,6}")), mainWidget);

  m_imageWidthEdit = WidgetsFactory::createLineEdit(QString(), mainWidget);
  m_imageWidthEdit->setObjectName(QLatin1String(kImageWidthEditName));
  m_imageWidthEdit->setValidator(sizeValidator);
  gridLayout->addWidget(new QLabel(tr("Width (px)"), mainWidget), 3, 0, 1, 1);
  gridLayout->addWidget(m_imageWidthEdit, 3, 1, 1, 1);

  m_imageHeightEdit = WidgetsFactory::createLineEdit(QString(), mainWidget);
  m_imageHeightEdit->setObjectName(QLatin1String(kImageHeightEditName));
  m_imageHeightEdit->setValidator(sizeValidator);
  gridLayout->addWidget(new QLabel(tr("Height (px)"), mainWidget), 3, 2, 1, 1);
  gridLayout->addWidget(m_imageHeightEdit, 3, 3, 1, 1);

  // Preview area.
  m_imageLabel = new QLabel(mainWidget);
  m_imageLabel->setScaledContents(true);
  m_previewArea = new QScrollArea(mainWidget);
  m_previewArea->setBackgroundRole(QPalette::Dark);
  m_previewArea->setWidget(m_imageLabel);
  m_previewArea->setMinimumSize(256, 256);
  gridLayout->addWidget(m_previewArea, 4, 0, 1, 5);

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
      setImage(ImageUtils::imageFromFile(localFile));
    } else {
      setImage(QImage());
    }

    m_source = Source::LocalFile;
  } else {
    setImage(QImage());
    m_source = Source::ImageData;

    if (!m_downloader) {
      m_downloader = new NetworkAccess(this);
      connect(m_downloader, &NetworkAccess::requestFinished, this,
              &ImageInsertDialog::handleImageDownloaded);
    }

    m_downloader->requestAsync(url);
  }

  m_imageTitleEdit->setText(QFileInfo(text).baseName());

  checkInput();
}

void ImageInsertDialog::checkInput() { setButtonEnabled(QDialogButtonBox::Ok, !m_image.isNull()); }

void ImageInsertDialog::browseFile() {
  if (!m_configMgr) {
    return;
  }
  auto &sessionConfig = m_configMgr->getSessionConfig();
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

int ImageInsertDialog::getImageWidth() const { return positiveIntOrZero(m_imageWidthEdit); }

int ImageInsertDialog::getImageHeight() const { return positiveIntOrZero(m_imageHeightEdit); }

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
    m_imageWidthEdit->setPlaceholderText(QString());
    m_imageHeightEdit->setPlaceholderText(QString());
  } else {
    m_imageLabel->setPixmap(QPixmap::fromImage(m_image));
    m_imageLabel->adjustSize();
    setImageControlsVisible(true);
    // A hint only -- never prefilled; see setupUI().
    m_imageWidthEdit->setPlaceholderText(QString::number(m_image.width()));
    m_imageHeightEdit->setPlaceholderText(QString::number(m_image.height()));
  }

  checkInput();
}

void ImageInsertDialog::setImagePath(const QString &p_path) {
  m_tempFile.reset();
  m_imagePathEdit->setText(p_path);
}

void ImageInsertDialog::handleImageDownloaded(const NetworkReply &p_data, const QString &p_url) {
  setImage(QImage::fromData(p_data.m_data));

  // Save it to a temp file to avoid potential data loss via QImage.
  bool savedToFile = false;
  if (!p_data.m_data.isEmpty()) {
    auto format = QFileInfo(PathUtils::removeUrlParameters(p_url)).suffix();
    m_tempFile.reset(FileUtils2::createTemporaryFile(format));
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
