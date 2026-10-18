#include "webviewservice.h"

#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include <utility>

#include "webviewjobs.h"

using namespace vnotex;

WebViewService::WebViewService(ServiceLocator &p_services, QWebEngineProfile *p_profile,
                               Bootstrap p_bootstrap, QObject *p_parent)
    : QObject(p_parent), m_profile(p_profile), m_bootstrap(std::move(p_bootstrap)) {
  Q_UNUSED(p_services);
  Q_ASSERT(p_profile && m_bootstrap);
}

WebViewService::~WebViewService() {
  m_destroying = true;
  invalidate();
}

WebViewJobSession *WebViewService::sharedSession() {
  if (m_destroying || !m_profile || !m_bootstrap) {
    return nullptr;
  }
  if (!m_session) {
    m_view = std::make_unique<QWebEngineView>();
    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->setAttribute(Qt::WA_ShowWithoutActivating);
    m_view->setFocusPolicy(Qt::NoFocus);
    m_view->resize(1024, 768);
    auto page = new QWebEnginePage(m_profile, m_view.get());
    m_view->setPage(page);
    auto channel = new QWebChannel(page);
    page->setWebChannel(channel);
    m_session = new WebViewJobSession(page, channel, page);
    // Keep a real raster surface alive without showing a pane or taking focus.
    m_view->show();
    m_bootstrap(page, channel);
  }
  return m_session;
}

void WebViewService::invalidate() {
  // Clear published pointers before invoking terminal callbacks. A callback can
  // request the next runtime without obtaining the retiring session.
  auto view = std::move(m_view);
  auto session = m_session.data();
  m_session = nullptr;
  delete session;
  // view owns page; the borrowed profile is never destroyed here.
}
