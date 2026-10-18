#ifndef TYPSTVIEWWINDOWCONTROLLER_H
#define TYPSTVIEWWINDOWCONTROLLER_H

#include "textviewwindowcontroller.h"

#include <QUrl>

#include <core/services/buffer2.h>

namespace vnotex {

class TypstViewWindowController : public TextViewWindowController {
  Q_OBJECT
public:
  explicit TypstViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  struct Project {
    QString root;
    QString mainFile;
  };

  bool setBuffer(const Buffer2 &p_buffer, QString *p_error = nullptr);
  const Project &getProject() const;
  void requestOpenSource(const QString &p_file, int p_start, int p_end);
  void requestOpenLink(const QUrl &p_url);
  void openConfirmedExternalLink(const QUrl &p_url);

signals:
  void errorOccurred(const QString &p_message);
  void externalLinkConfirmationRequested(const QUrl &p_url);

private:
  bool bufferAvailable() const;
  NodeIdentifier resolveIndexedFile(const QString &p_absolutePath) const;
  void confirmExternalLink(const QUrl &p_url);

  ServiceLocator &m_services;
  Buffer2 m_buffer;
  Project m_project;
  quint64 m_generation = 0;
  QUrl m_pendingExternalLink;
  quint64 m_pendingExternalGeneration = 0;
  bool m_externalConfirmationActive = false;
};

} // namespace vnotex

#endif // TYPSTVIEWWINDOWCONTROLLER_H
