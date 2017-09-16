#include <QtWidgets>
#include <QValidator>
#include <QRegExp>
#include "vinsertimagedialog.h"
#include "utils/vutils.h"

VInsertImageDialog::VInsertImageDialog(const QString &title, const QString &defaultImageTitle,
                                       const QString &defaultPath, QWidget *parent)
    : QDialog(parent), title(title), defaultImageTitle(defaultImageTitle), defaultPath(defaultPath),
      image(NULL)
{
    setupUI();

    connect(imageTitleEdit, &QLineEdit::textChanged, this, &VInsertImageDialog::enableOkButton);
    connect(pathEdit, &QLineEdit::textChanged, this, &VInsertImageDialog::enableOkButton);
    connect(browseBtn, &QPushButton::clicked, this, &VInsertImageDialog::handleBrowseBtnClicked);

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

    imageTitleLabel = new QLabel(tr("&Image title:"));
    imageTitleEdit = new QLineEdit(defaultImageTitle);
    imageTitleEdit->selectAll();
    imageTitleLabel->setBuddy(imageTitleEdit);
    QRegExp regExp("[\\w\\(\\)@#%\\*\\-\\+=\\?<>\\,\\.\\s]+");
    QValidator *validator = new QRegExpValidator(regExp, this);
    imageTitleEdit->setValidator(validator);

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(pathLabel, 0, 0);
    topLayout->addWidget(pathEdit, 0, 1);
    topLayout->addWidget(browseBtn, 0, 2);
    topLayout->addWidget(imageTitleLabel, 1, 0);
    topLayout->addWidget(imageTitleEdit, 1, 1, 1, 2);
    topLayout->setColumnStretch(0, 0);
    topLayout->setColumnStretch(1, 1);
    topLayout->setColumnStretch(2, 0);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    imagePreviewLabel = new QLabel();
    imagePreviewLabel->setVisible(false);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    mainLayout->addWidget(imagePreviewLabel);
    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(title);

    imageTitleEdit->setFocus();
}

void VInsertImageDialog::enableOkButton()
{
    bool enabled = true;
    if (imageTitleEdit->text().isEmpty() || !image) {
        enabled = false;
    }
    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
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
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select The Image To Be Inserted"),
                                                    lastPath, tr("Images (*.png *.xpm *.jpg *.bmp *.gif)"));
    if (filePath.isEmpty()) {
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
    int width = 512 * VUtils::calculateScaleFactor();
    QSize previewSize(width, width);
    if (!this->image) {
        this->image = new QImage(image);
    } else {
        *(this->image) = image;
    }
    imagePreviewLabel->setPixmap(QPixmap::fromImage(this->image->scaled(previewSize)));
    imagePreviewLabel->setVisible(true);
    enableOkButton();
}

void VInsertImageDialog::setBrowseable(bool browseable, bool visible)
{
    pathEdit->setReadOnly(!browseable);
    browseBtn->setEnabled(browseable);

    pathLabel->setVisible(visible);
    pathEdit->setVisible(visible);
    browseBtn->setVisible(visible);
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
