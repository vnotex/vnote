#ifndef VINSERTIMAGEDIALOG_H
#define VINSERTIMAGEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QString>
#include <QByteArray>

class QLabel;
class QLineEdit;
class QPushButton;

class VInsertImageDialog : public QDialog
{
    Q_OBJECT
public:
    VInsertImageDialog(const QString &title, const QString &defaultImageTitle,
                       const QString &defaultPath,
                       QWidget *parent = 0);
    ~VInsertImageDialog();
    QString getImageTitleInput() const;
    QString getPathInput() const;

    void setImage(const QImage &image);
    QImage getImage() const;
    void setBrowseable(bool browseable);

public slots:
    void imageDownloaded(const QByteArray &data);

private slots:
    void enableOkButton();
    void handleBrowseBtnClicked();

private:
    void setupUI();

    QLabel *imageTitleLabel;
    QLineEdit *imageTitleEdit;
    QLabel *pathLabel;
    QLineEdit *pathEdit;
    QPushButton *browseBtn;
    QPushButton *okBtn;
    QPushButton *cancelBtn;
    QLabel *imagePreviewLabel;

    QString title;
    QString defaultImageTitle;
    QString defaultPath;
    QImage *image;
    bool browseable;
};

#endif // VINSERTIMAGEDIALOG_H
