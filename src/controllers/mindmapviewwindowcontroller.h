#ifndef MINDMAPVIEWWINDOWCONTROLLER_H
#define MINDMAPVIEWWINDOWCONTROLLER_H

#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <core/services/buffer2.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace vnotex {

class ServiceLocator;

class MindMapViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit MindMapViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~MindMapViewWindowController() override;

  void setBuffer(const Buffer2 &p_buffer);
  void requestImage(const QString &p_url, quint64 p_requestId);
  QByteArray readImageData(const QString &p_source, QString *p_error);
  QString insertImage(const QByteArray &p_data, const QString &p_title, bool p_base64,
                      QString *p_createdAsset, QString *p_error);
  QString insertAttachment(const QString &p_filePath, QString *p_createdAttachment,
                           QString *p_error);
  QString relativeLink(const QUrl &p_url) const;
  void requestOpenLink(const QString &p_url);
  void openConfirmedExternalLink(const QUrl &p_url);

signals:
  void imageReady(const QString &p_url, quint64 p_requestId, const QImage &p_image);
  void errorOccurred(const QString &p_message);
  void externalLinkConfirmationRequested(const QUrl &p_url);

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  void invalidateRequests();
  bool bufferAvailable() const;
  bool isCurrent(quint64 p_generation, const std::shared_ptr<ProtectedBufferLease> &p_lease) const;
  void requestNetworkImage(const QUrl &p_source, const QString &p_url, quint64 p_requestId);
  void confirmExternalLink(const QUrl &p_url);

  ServiceLocator &m_services;
  Buffer2 m_buffer;
  QNetworkAccessManager *m_imageNetwork = nullptr;
  QSet<QNetworkReply *> m_imageReplies;
  quint64 m_generation = 0;
  QUrl m_pendingExternalLink;
  quint64 m_pendingExternalGeneration = 0;
};

} // namespace vnotex

#endif // MINDMAPVIEWWINDOWCONTROLLER_H
