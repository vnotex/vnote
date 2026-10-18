#ifndef WEBVIEWSERVICE_H
#define WEBVIEWSERVICE_H

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QWebChannel;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;

namespace vnotex {

class ServiceLocator;
class WebViewJobSession;

// One application-owned ordinary runtime, independent of its providers. The
// borrowed ordinary profile must outlive this service. Protected runtimes belong
// to their consumers and attach WebViewJobSession directly instead.
class WebViewService : public QObject {
  Q_OBJECT

public:
  using Bootstrap = std::function<void(QWebEnginePage *, QWebChannel *)>;

  WebViewService(ServiceLocator &p_services, QWebEngineProfile *p_profile, Bootstrap p_bootstrap,
                 QObject *p_parent = nullptr);
  ~WebViewService() override;

  WebViewJobSession *sharedSession();
  void invalidate();

private:
  QPointer<QWebEngineProfile> m_profile;
  Bootstrap m_bootstrap;
  std::unique_ptr<QWebEngineView> m_view;
  QPointer<WebViewJobSession> m_session;
  bool m_destroying = false;
};

} // namespace vnotex

#endif // WEBVIEWSERVICE_H
