#ifndef VINSERTIMAGEDIALOG_H
#define VINSERTIMAGEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QString>
#include <QByteArray>
#include <QTemporaryFile>
#include <QSharedPointer>

class QLabel;
class VLineEdit;
class VMetaWordLineEdit;
class QPushButton;
class QDialogButtonBox;
class QScrollArea;
class QSpinBox;
class QSlider;

class VInsertImageDialog : public QDialog
{
    Q_OBJECT
public:
    enum ImageType
    {
        LocalFile = 0,
        ImageData
    };

    VInsertImageDialog(const QString &p_title,
                       const QString &p_imageTitle,
                       const QString &p_imagePath,
                       bool p_browsable = true,
                       QWidget *p_parent = nullptr);

    QString getImageTitleInput() const;

    QString getPathInput() const;

    void setImage(const QImage &p_image);

    QImage getImage() const;

    VInsertImageDialog::ImageType getImageType() const;

    // Return 0 if no override.
    int getOverridenWidth() const;

public slots:
    void imageDownloaded(const QByteArray &data);

private slots:
    void handleInputChanged();

    void handleBrowseBtnClicked();

    void handlePathEditChanged();

private:
    void setupUI(const QString &p_title,
                 const QString &p_imageTitle,
                 const QString &p_imagePath);

    void fetchImageFromClipboard();

    void setPath(const QString &p_path);

    void setImageControlsVisible(bool p_visible);

    VMetaWordLineEdit *m_imageTitleEdit;
    VLineEdit *m_pathEdit;
    QPushButton *browseBtn;

    QSpinBox *m_widthSpin;
    QSlider *m_scaleSlider;
    QLabel *m_sliderLabel;

    QDialogButtonBox *m_btnBox;

    QLabel *m_imageLabel;
    QScrollArea *m_previewArea;

    QSharedPointer<QImage> m_image;

    // Whether enable the browse action.
    bool m_browsable;

    ImageType m_imageType;

    QSharedPointer<QTemporaryFile> m_tempFile;
};

inline VInsertImageDialog::ImageType VInsertImageDialog::getImageType() const
{
    return m_imageType;
}

#endif // VINSERTIMAGEDIALOG_H
