#include <QtWidgets>
#include <QValidator>
#include <QRegExp>
#include <QDebug>
#include "vinsertimagedialog.h"
#include "utils/vutils.h"
#include "vlineedit.h"
#include "vdownloader.h"

VInsertImageDialog::VInsertImageDialog(const QString &p_title,
                                       const QString &p_imageTitle,
                                       const QString &p_imagePath,
                                       bool p_browsable,
                                       QWidget *p_parent)
    : QDialog(p_parent),
      m_image(NULL),
      m_browsable(p_browsable)
{
    setupUI(p_title, p_imageTitle, p_imagePath);

    connect(m_imageTitleEdit, &QLineEdit::textChanged,
            this, &VInsertImageDialog::handleInputChanged);
    connect(pathEdit, &QLineEdit::textChanged,
            this, &VInsertImageDialog::handleInputChanged);
    connect(browseBtn, &QPushButton::clicked,
            this, &VInsertImageDialog::handleBrowseBtnClicked);

    fetchImageFromClipboard();

    handleInputChanged();
}

VInsertImageDialog::~VInsertImageDialog()
{
    if (m_image) {
        delete m_image;
        m_image = NULL;
    }
}

void VInsertImageDialog::setupUI(const QString &p_title,
                                 const QString &p_imageTitle,
                                 const QString &p_imagePath)
{
    QLabel *pathLabel = new QLabel(tr("&From:"));
    pathEdit = new QLineEdit(p_imagePath);
    pathLabel->setBuddy(pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));
    pathEdit->setReadOnly(!m_browsable);
    browseBtn->setEnabled(m_browsable);

    QLabel *imageTitleLabel = new QLabel(tr("&Image title:"));
    m_imageTitleEdit = new VLineEdit(p_imageTitle);
    m_imageTitleEdit->selectAll();
    imageTitleLabel->setBuddy(m_imageTitleEdit);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_imageTitleRegExp),
                                                 m_imageTitleEdit);
    m_imageTitleEdit->setValidator(validator);

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(pathLabel, 0, 0);
    topLayout->addWidget(pathEdit, 0, 1);
    topLayout->addWidget(browseBtn, 0, 2);
    topLayout->addWidget(imageTitleLabel, 1, 0);
    topLayout->addWidget(m_imageTitleEdit, 1, 1, 1, 2);
    topLayout->setColumnStretch(0, 0);
    topLayout->setColumnStretch(1, 1);
    topLayout->setColumnStretch(2, 0);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_btnBox->button(QDialogButtonBox::Ok)->setProperty("SpecialBtn", true);

    imagePreviewLabel = new QLabel();
    imagePreviewLabel->setVisible(false);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    mainLayout->addWidget(imagePreviewLabel);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);

    m_imageTitleEdit->setFocus();
}

void VInsertImageDialog::handleInputChanged()
{
    bool pathOk = true;
    if (m_browsable && m_imageType == ImageType::LocalFile) {
        QString path = pathEdit->text();
        if (path.isEmpty()
            || (!VUtils::checkPathLegal(path) && !QUrl(path).isValid())) {
            pathOk = false;
        }
    }

    QString title = m_imageTitleEdit->getEvaluatedText();
    QRegExp reg(VUtils::c_imageTitleRegExp);
    bool titleOk = reg.exactMatch(title);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(pathOk && titleOk && m_image);
}

QString VInsertImageDialog::getImageTitleInput() const
{
    return m_imageTitleEdit->getEvaluatedText();
}

QString VInsertImageDialog::getPathInput() const
{
    return pathEdit->text();
}

void VInsertImageDialog::handleBrowseBtnClicked()
{
    static QString lastPath = QDir::homePath();
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select The Image To Be Inserted"),
                                                    lastPath, tr("Images (*.png *.xpm *.jpg *.bmp *.gif)"));
    if (filePath.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(filePath).path();

    pathEdit->setText(filePath);

    m_imageType = ImageType::LocalFile;

    QImage im(filePath);
    if (im.isNull()) {
        if (m_image) {
            delete m_image;
            m_image = NULL;

            imagePreviewLabel->setVisible(false);
        }

        return;
    }

    setImage(im);

    m_imageTitleEdit->setFocus();
}

void VInsertImageDialog::setImage(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "set Null image";
        return;
    }

    int width = 512 * VUtils::calculateScaleFactor();
    QSize previewSize(width, width);
    if (!m_image) {
        m_image = new QImage(image);
    } else {
        *m_image = image;
    }

    QPixmap pixmap;
    if (image.width() > width || image.height() > width) {
        pixmap = QPixmap::fromImage(m_image->scaled(previewSize, Qt::KeepAspectRatio));
    } else {
        pixmap = QPixmap::fromImage(*m_image);
    }

    imagePreviewLabel->setPixmap(pixmap);
    imagePreviewLabel->setVisible(true);

    handleInputChanged();
}

void VInsertImageDialog::imageDownloaded(const QByteArray &data)
{
    setImage(QImage::fromData(data));
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
    if (!m_browsable || !pathEdit->text().isEmpty()) {
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
        qDebug() << "fetch image data from clipboard to insert";
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
        QImage image(url.toLocalFile());
        if (image.isNull()) {
            // Try to treat it as network image.
            // Download it to a QImage
            VDownloader *downloader = new VDownloader(this);
            connect(downloader, &VDownloader::downloadFinished,
                    this, &VInsertImageDialog::imageDownloaded);
            downloader->download(url.toString());
            pathEdit->setText(url.toString());
            m_imageType = ImageType::ImageData;
            qDebug() << "try to fetch network image from clipboard to insert" << url;
        } else {
            setImage(image);
            pathEdit->setText(url.toLocalFile());
            m_imageType = ImageType::LocalFile;
            qDebug() << "fetch local file image from clipboard to insert" << url;
        }
    }
}
