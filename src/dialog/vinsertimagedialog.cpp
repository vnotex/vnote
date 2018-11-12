#include <QtWidgets>
#include <QValidator>
#include <QRegExp>

#include "vinsertimagedialog.h"
#include "utils/vutils.h"
#include "vmetawordlineedit.h"
#include "vdownloader.h"
#include "vlineedit.h"

VInsertImageDialog::VInsertImageDialog(const QString &p_title,
                                       const QString &p_imageTitle,
                                       const QString &p_imagePath,
                                       bool p_browsable,
                                       QWidget *p_parent)
    : QDialog(p_parent),
      m_browsable(p_browsable)
{
    setupUI(p_title, p_imageTitle, p_imagePath);

    connect(m_imageTitleEdit, &VMetaWordLineEdit::textChanged,
            this, &VInsertImageDialog::handleInputChanged);

    if (m_browsable) {
        connect(m_pathEdit, &VLineEdit::editingFinished,
                this, &VInsertImageDialog::handlePathEditChanged);

        connect(browseBtn, &QPushButton::clicked,
                this, &VInsertImageDialog::handleBrowseBtnClicked);

        fetchImageFromClipboard();
    }

    autoCompleteTitleFromPath();

    handleInputChanged();
}

void VInsertImageDialog::setupUI(const QString &p_title,
                                 const QString &p_imageTitle,
                                 const QString &p_imagePath)
{
    // Path.
    m_pathEdit = new VLineEdit(p_imagePath);
    m_pathEdit->setReadOnly(!m_browsable);
    browseBtn = new QPushButton(tr("&Browse"));
    browseBtn->setEnabled(m_browsable);

    // Title.
    m_imageTitleEdit = new VMetaWordLineEdit(p_imageTitle);
    m_imageTitleEdit->selectAll();
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_imageTitleRegExp),
                                                 m_imageTitleEdit);
    m_imageTitleEdit->setValidator(validator);

    // Scale.
    m_widthSpin = new QSpinBox();
    m_widthSpin->setMinimum(1);
    m_widthSpin->setSingleStep(10);
    m_widthSpin->setSuffix(" px");
    connect(m_widthSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, [this](int p_val) {
                if (!m_image) {
                    return;
                }

                int height = m_image->height() * (1.0 * p_val / m_image->width());
                m_imageLabel->resize(p_val, height);
            });

    // 0.1 to 2.0 -> 1 to 20.
    m_scaleSlider = new QSlider();
    m_scaleSlider->setOrientation(Qt::Horizontal);
    m_scaleSlider->setMinimum(1);
    m_scaleSlider->setMaximum(20);
    m_scaleSlider->setValue(10);
    m_scaleSlider->setSingleStep(1);
    m_scaleSlider->setPageStep(5);
    connect(m_scaleSlider, &QSlider::valueChanged,
            this, [this](int p_val) {
                if (!m_image) {
                    return;
                }

                int width = m_image->width();
                qreal factor = 1.0;
                if (p_val != 10) {
                    factor = p_val / 10.0;
                    width = m_image->width() * factor;
                }

                m_widthSpin->setValue(width);
                m_sliderLabel->setText(QString::number(factor) + "x");
            });

    m_sliderLabel = new QLabel("1x");

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(new QLabel(tr("From:")), 0, 0, 1, 1);
    topLayout->addWidget(m_pathEdit, 0, 1, 1, 3);
    topLayout->addWidget(browseBtn, 0, 4, 1, 1);
    topLayout->addWidget(new QLabel(tr("Title:")), 1, 0, 1, 1);
    topLayout->addWidget(m_imageTitleEdit, 1, 1, 1, 4);
    topLayout->addWidget(new QLabel(tr("Scaling width:")), 2, 0, 1, 1);
    topLayout->addWidget(m_widthSpin, 2, 1, 1, 1);
    topLayout->addWidget(m_scaleSlider, 2, 2, 1, 2);
    topLayout->addWidget(m_sliderLabel, 2, 4, 1, 1);

    topLayout->setColumnStretch(0, 0);
    topLayout->setColumnStretch(1, 0);
    topLayout->setColumnStretch(2, 1);
    topLayout->setColumnStretch(3, 1);
    topLayout->setColumnStretch(4, 0);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_btnBox->button(QDialogButtonBox::Ok)->setProperty("SpecialBtn", true);

    m_imageLabel = new QLabel();
    m_imageLabel->setScaledContents(true);

    m_previewArea = new QScrollArea();
    m_previewArea->setBackgroundRole(QPalette::Dark);
    m_previewArea->setWidget(m_imageLabel);
    int minWidth = 512 * VUtils::calculateScaleFactor();
    m_previewArea->setMinimumSize(minWidth, minWidth);

    setImageControlsVisible(false);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    mainLayout->addWidget(m_previewArea);
    setLayout(mainLayout);

    setWindowTitle(p_title);

    m_imageTitleEdit->setFocus();
}

void VInsertImageDialog::handleInputChanged()
{
    QString title = m_imageTitleEdit->getEvaluatedText();
    QRegExp reg(VUtils::c_imageTitleRegExp);
    bool titleOk = reg.exactMatch(title);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(titleOk && m_image);
}

QString VInsertImageDialog::getImageTitleInput() const
{
    return m_imageTitleEdit->getEvaluatedText();
}

QString VInsertImageDialog::getPathInput() const
{
    if (m_tempFile.isNull()) {
        return m_pathEdit->text();
    } else {
        return m_tempFile->fileName();
    }
}

void VInsertImageDialog::handleBrowseBtnClicked()
{
    static QString lastPath = QDir::homePath();
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select The Image To Be Inserted"),
                                                    lastPath, tr("Images (*.png *.xpm *.jpg *.bmp *.gif *.svg)"));
    if (filePath.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(filePath).path();

    m_imageType = ImageType::LocalFile;

    setPath(filePath);

    autoCompleteTitleFromPath();

    m_imageTitleEdit->setFocus();
}

void VInsertImageDialog::setImage(const QImage &p_image)
{
    if (p_image.isNull()) {
        m_image.clear();
        m_imageLabel->clear();

        setImageControlsVisible(false);
    } else {
        m_image.reset(new QImage(p_image));

        m_imageLabel->setPixmap(QPixmap::fromImage(*m_image));

        m_imageLabel->adjustSize();

        // Set the scaling widgets.
        m_scaleSlider->setValue(10);

        int width = m_image->width();
        m_widthSpin->setMaximum(width * 5);
        m_widthSpin->setValue(width);

        setImageControlsVisible(true);
    }

    handleInputChanged();
}

void VInsertImageDialog::imageDownloaded(const QByteArray &data)
{
    setImage(QImage::fromData(data));

    // Try to save it to a temp file.
    {
    if (data.isEmpty()) {
        goto image_data;
    }

    QString format = QFileInfo(VUtils::purifyUrl(getPathInput())).suffix();
    if (format.isEmpty()) {
        goto image_data;
    }

    m_tempFile.reset(VUtils::createTemporaryFile(format));
    if (!m_tempFile->open()) {
        goto image_data;
    }

    if (m_tempFile->write(data) == -1) {
        goto image_data;
    }

    m_imageType = ImageType::LocalFile;
    m_tempFile->close();
    return;
    }

image_data:
    m_tempFile.clear();
    m_imageType = ImageType::ImageData;
}

QImage VInsertImageDialog::getImage() const
{
    if (!m_image) {
        return QImage();
    } else return *m_image;
}

void VInsertImageDialog::fetchImageFromClipboard()
{
    if (!m_browsable || !m_pathEdit->text().isEmpty()) {
        return;
    }

    Q_ASSERT(!m_image);

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    QUrl url;

    if (mimeData->hasImage()) {
        QImage im = qvariant_cast<QImage>(mimeData->imageData());
        if (im.isNull()) {
            return;
        }

        setImage(im);
        m_imageType = ImageType::ImageData;
        return;
    } else if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        if (urls.size() != 1) {
            return;
        }

        url = urls[0];
    } else if (mimeData->hasText()) {
        url = QUrl(mimeData->text());
    }

    if (url.isValid()) {
        if (url.isLocalFile()) {
            setPath(url.toLocalFile());
        } else {
            setPath(url.toString());
        }
    }
}

void VInsertImageDialog::handlePathEditChanged()
{
    QString text = m_pathEdit->text();
    QUrl url = QUrl::fromUserInput(text);
    if (text.isEmpty() || !url.isValid()) {
        setImage(QImage());
        return;
    }

    QImage image;
    if (url.isLocalFile()) {
        image = VUtils::imageFromFile(url.toLocalFile());
        setImage(image);
        m_imageType = ImageType::LocalFile;
    } else {
        setImage(QImage());
        m_imageType = ImageType::ImageData;
        VDownloader *downloader = new VDownloader(this);
        connect(downloader, &VDownloader::downloadFinished,
                this, &VInsertImageDialog::imageDownloaded);
        downloader->download(url.toString());
    }

    handleInputChanged();
}

void VInsertImageDialog::setPath(const QString &p_path)
{
    m_pathEdit->setText(p_path);
    handlePathEditChanged();
}

void VInsertImageDialog::setImageControlsVisible(bool p_visible)
{
    m_widthSpin->setEnabled(p_visible);
    m_scaleSlider->setEnabled(p_visible);
    m_sliderLabel->setEnabled(p_visible);

    m_previewArea->setVisible(p_visible);
}

int VInsertImageDialog::getOverridenWidth() const
{
    int width = m_widthSpin->value();
    if (m_image && m_image->width() != width) {
        return width;
    }

    return 0;
}

void VInsertImageDialog::autoCompleteTitleFromPath()
{
    if (!m_imageTitleEdit->text().isEmpty()) {
        return;
    }

    QString imgPath = m_pathEdit->text();
    if (imgPath.isEmpty()) {
        return;
    }

    m_imageTitleEdit->setText(QFileInfo(imgPath).baseName());
    m_imageTitleEdit->selectAll();
}
