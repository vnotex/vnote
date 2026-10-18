#ifndef MARKDOWNWEBJOBHOST_H
#define MARKDOWNWEBJOBHOST_H

#include <QByteArray>
#include <QObject>
#include <memory>

#include <gui/services/webviewservice.h>

namespace vnotex {
class Buffer2;
class ServiceLocator;
class WebViewJobSession;

// View-side runtime construction. Ordinary bootstrap captures application services only.
class MarkdownWebJobHost : public QObject {
  Q_OBJECT
public:
  MarkdownWebJobHost(ServiceLocator &p_services, const Buffer2 &p_buffer,
                     QObject *p_parent = nullptr);
  ~MarkdownWebJobHost() override;

  static WebViewService::Bootstrap createSharedRuntime(ServiceLocator &p_services);

public slots:
  void ensureProtectedSession();
  void releaseProtectedSession();
  void invalidate();

signals:
  // Emitted when attached, not when JavaScript is ready. Null is a terminal creation failure.
  void protectedSessionReady(WebViewJobSession *p_session, const QByteArray &p_highlightCss);

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  struct ProtectedRuntime;
  ServiceLocator &m_services;
  const Buffer2 &m_buffer;
  std::unique_ptr<ProtectedRuntime> m_protected;
  int m_closeHook = -1;
  int m_notebookCloseHook = -1;
};
} // namespace vnotex

#endif // MARKDOWNWEBJOBHOST_H
