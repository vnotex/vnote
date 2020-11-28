#ifndef IMAGEINSERTDIALOG_H
#define IMAGEINSERTDIALOG_H

#include "dialog.h"

#include <QSharedPointer>
#include <QImage>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QSlider;
class QLabel;
class QTimer;
class QTemporaryFile;
class QScrollArea;

namespace vte
{
    class Downloader;
}

namespace vnotex
{
    class ImageInsertDialog : public Dialog
    {
        Q_OBJECT
    public:
        enum Source
        {
            LocalFile,
            ImageData
        };

        ImageInsertDialog(const QString &p_title,
                          const QString &p_imageTitle,
                          const QString &p_imageAlt,
                          const QString &p_imagePath,
                          bool p_browserEnabled = true,
                          QWidget *p_parent = nullptr);

        QString getImageTitle() const;

        QString getImageAltText() const;

        QString getImagePath() const;
        void setImagePath(const QString &p_path);

        ImageInsertDialog::Source getImageSource() const;
        void setImageSource(ImageInsertDialog::Source p_source);

        const QImage &getImage() const;
        void setImage(const QImage &p_image);

        // Return 0 if no scaling.
        int getScaledWidth() const;

    protected:
        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void checkImagePathInput();

        void checkInput();

        void browseFile();

        void handleImageDownloaded(const QByteArray &p_data, const QString &p_url);

        void handleScaleSliderValueChanged(int p_val);

    private:
        void setupUI(const QString &p_title,
                     const QString &p_imageTitle,
                     const QString &p_imageAlt,
                     const QString &p_imagePath);

        void setImageControlsVisible(bool p_visible);

        bool m_browserEnabled = true;

        Source m_source = Source::LocalFile;

        QLineEdit *m_imagePathEdit = nullptr;

        QPushButton *m_browseBtn = nullptr;

        QLineEdit *m_imageTitleEdit = nullptr;

        QLineEdit *m_imageAltEdit = nullptr;

        QSpinBox *m_widthSpin = nullptr;

        QSlider *m_scaleSlider = nullptr;

        QLabel *m_sliderLabel = nullptr;

        QLabel *m_imageLabel = nullptr;

        QScrollArea *m_previewArea = nullptr;

        QImage m_image;

        // Managed by QObject.
        vte::Downloader *m_downloader = nullptr;

        // Managed by QObject.
        QTimer *m_imagePathCheckTimer = nullptr;

        // Used to hold downloaded image, to avoid data loss via QImage.
        QSharedPointer<QTemporaryFile> m_tempFile;

        static int s_lastScaleSliderValue;
    };
}

#endif // IMAGEINSERTDIALOG_H
