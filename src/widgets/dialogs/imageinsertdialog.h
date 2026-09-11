#ifndef IMAGEINSERTDIALOG_H
#define IMAGEINSERTDIALOG_H

#include "dialog.h"

#include <QByteArray>
#include <QImage>

class QLineEdit;
class QPushButton;
class QRadioButton;
class QLabel;
class QTimer;
class QScrollArea;

namespace vnotex {
class ConfigMgr2;
class NetworkAccess;
struct NetworkReply;

class ImageInsertDialog : public Dialog {
  Q_OBJECT
public:
  enum Source { LocalFile, ImageData };

  ImageInsertDialog(const QString &p_title, const QString &p_imageTitle, const QString &p_imageAlt,
                    const QString &p_imagePath, ConfigMgr2 *p_configMgr,
                    bool p_browserEnabled = true, QWidget *p_parent = nullptr);

  QString getImageTitle() const;

  QString getImageAltText() const;

  // Optional size, in pixels. 0 means "not specified", which is what keeps an
  // ordinary insert a Markdown link: any nonzero value makes the caller emit an
  // HTML `<img>` instead.
  int getImageWidth() const;

  int getImageHeight() const;

  QString getImagePath() const;
  void setImagePath(const QString &p_path);

  ImageInsertDialog::Source getImageSource() const;
  void setImageSource(ImageInsertDialog::Source p_source);

  const QImage &getImage() const;
  void setImage(const QImage &p_image);

  // Original source bytes, or lossless PNG for a clipboard QImage. Never staged on disk.
  QByteArray getImageData() const;

  // Encrypted notes default to Base64 and only preview validated passive bytes.
  void setEncryptedNote(bool p_encrypted);
  void setInsertAsBase64(bool p_base64);
  bool insertAsBase64() const;

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
  void checkImagePathInput();

  void checkInput();

  void browseFile();

  void handleImageDownloaded(const vnotex::NetworkReply &p_data, const QString &p_url);

private:
  void setupUI(const QString &p_title, const QString &p_imageTitle, const QString &p_imageAlt,
               const QString &p_imagePath);

  void setImageControlsVisible(bool p_visible);
  void setSourceImageData(const QByteArray &p_data);

  bool m_browserEnabled = true;
  bool m_encryptedNote = false;

  // ConfigMgr2 (owner-supplied) for the session-scoped default media path.
  ConfigMgr2 *m_configMgr = nullptr;

  Source m_source = Source::LocalFile;

  QLineEdit *m_imagePathEdit = nullptr;

  QPushButton *m_browseBtn = nullptr;

  QLineEdit *m_imageTitleEdit = nullptr;

  QLineEdit *m_imageAltEdit = nullptr;

  QLineEdit *m_imageWidthEdit = nullptr;

  QLineEdit *m_imageHeightEdit = nullptr;

  QRadioButton *m_imageFileRadio = nullptr;
  QRadioButton *m_base64Radio = nullptr;
  QLabel *m_imageLabel = nullptr;

  QScrollArea *m_previewArea = nullptr;

  QImage m_image;
  QByteArray m_imageData;

  // Managed by QObject.
  vnotex::NetworkAccess *m_downloader = nullptr;

  // Managed by QObject.
  QTimer *m_imagePathCheckTimer = nullptr;
};
} // namespace vnotex

#endif // IMAGEINSERTDIALOG_H
