#ifndef MINDMAPIMAGELOADER_H
#define MINDMAPIMAGELOADER_H

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace vnotex {

class ServiceLocator;

class MindMapImageLoader : public QObject {
  Q_OBJECT
public:
  explicit MindMapImageLoader(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~MindMapImageLoader() override;

  void requestImage(const QString &p_url, quint64 p_requestId, const QString &p_resourceBasePath);
  void cancel();

  static bool readImageFile(const QString &p_path, QByteArray &p_data);
  static bool decodeImageUrl(const QString &p_url, QByteArray &p_data);
  static QImage decodeImage(QByteArray &p_data);

signals:
  void imageReady(const QString &p_url, quint64 p_requestId, const QImage &p_image);

private:
  void requestNetworkImage(const QUrl &p_source, const QString &p_url, quint64 p_requestId);

  QNetworkAccessManager *m_imageNetwork = nullptr;
  QSet<QNetworkReply *> m_imageReplies;
  quint64 m_generation = 0;
};

} // namespace vnotex

#endif // MINDMAPIMAGELOADER_H
