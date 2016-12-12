#include <QtWidgets>
#include <QValidator>
#include <QRegExp>
#include "vinsertimagedialog.h"

VInsertImageDialog::VInsertImageDialog(const QString &title, const QString &defaultImageTitle,
                                       const QString &defaultPath, QWidget *parent)
    : QDialog(parent), title(title), defaultImageTitle(defaultImageTitle), defaultPath(defaultPath),
      image(NULL), browseable(true)
{
    setupUI();

    connect(imageTitleEdit, &QLineEdit::textChanged, this, &VInsertImageDialog::enableOkButton);
    connect(pathEdit, &QLineEdit::textChanged, this, &VInsertImageDialog::enableOkButton);
    connect(browseBtn, &QPushButton::clicked, this, &VInsertImageDialog::handleBrowseBtnClicked);
    connect(okBtn, &QPushButton::clicked, this, &VInsertImageDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &VInsertImageDialog::reject);

    enableOkButton();
}

VInsertImageDialog::~VInsertImageDialog()
{
    if (image) {
        delete image;
        image = NULL;
    }
}

void VInsertImageDialog::setupUI()
{
    pathLabel = new QLabel(tr("&From:"));
    pathEdit = new QLineEdit(defaultPath);
    pathLabel->setBuddy(pathEdit);
    browseBtn = new QPushButton(tr("&Browse"));
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(pathEdit);
    pathLayout->addWidget(browseBtn);

    imageTitleLabel = new QLabel(tr("&Title:"));
    imageTitleEdit = new QLineEdit(defaultImageTitle);
    imageTitleEdit->selectAll();
    imageTitleLabel->setBuddy(imageTitleEdit);
    QRegExp regExp("[\\w\\(\\)@#%\\*\\-\\+=\\?<>\\,\\.\\s]+");
    QValidator *validator = new QRegExpValidator(regExp, this);
    imageTitleEdit->setValidator(validator);
    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(imageTitleLabel);
    titleLayout->addWidget(imageTitleEdit);

    okBtn = new QPushButton(tr("&OK"));
    okBtn->setDefault(true);
    cancelBtn = new QPushButton(tr("&Cancel"));

    QVBoxLayout *topLayout = new QVBoxLayout();
    topLayout->addLayout(pathLayout);
    topLayout->addLayout(titleLayout);

    QHBoxLayout *btmLayout = new QHBoxLayout();
    btmLayout->addStretch();
    btmLayout->addWidget(okBtn);
    btmLayout->addWidget(cancelBtn);

    imagePreviewLabel = new QLabel();
    imagePreviewLabel->setVisible(false);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(btmLayout);
    mainLayout->addWidget(imagePreviewLabel);
    setLayout(mainLayout);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);
}

void VInsertImageDialog::enableOkButton()
{
    bool enabled = true;
    if (imageTitleEdit->text().isEmpty() || !image) {
        enabled = false;
    }
    okBtn->setEnabled(enabled);
}

QString VInsertImageDialog::getImageTitleInput() const
{
    return imageTitleEdit->text();
}

QString VInsertImageDialog::getPathInput() const
{
    return pathEdit->text();
}

void VInsertImageDialog::handleBrowseBtnClicked()
{
    static QString lastPath = QDir::homePath();
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select the image to be inserted"),
                                                    lastPath, tr("Images (*.png *.xpm *.jpg *.bmp *.gif)"));
    if (filePath.isNull() || filePath.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(filePath).path();

    pathEdit->setText(filePath);
    QImage image(filePath);
    if (image.isNull()) {
        return;
    }
    setImage(image);
}

void VInsertImageDialog::setImage(const QImage &image)
{
    Q_ASSERT(!image.isNull());
    QSize previewSize(256, 256);
    if (!this->image) {
        this->image = new QImage(image);
    } else {
        *(this->image) = image;
    }
    imagePreviewLabel->setPixmap(QPixmap::fromImage(this->image->scaled(previewSize)));
    imagePreviewLabel->setVisible(true);
    enableOkButton();
}

void VInsertImageDialog::setBrowseable(bool browseable)
{
    this->browseable = browseable;
    pathLabel->setVisible(browseable);
    pathEdit->setVisible(browseable);
    browseBtn->setVisible(browseable);
}

void VInsertImageDialog::imageDownloaded(const QByteArray &data)
{
    setImage(QImage::fromData(data));
}

QImage VInsertImageDialog::getImage() const
{
    if (!image) {
        return QImage();
    } else return *image;
}
