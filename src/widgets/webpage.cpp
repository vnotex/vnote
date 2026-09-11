#include "webpage.h"

#include <QDebug>
#include <QWebEngineProfile>

#include <core/logging.h>

using namespace vnotex;

WebPage::WebPage(QWidget *p_parent) : QWebEnginePage(p_parent) {}

WebPage::WebPage(QWebEngineProfile *p_profile, QWidget *p_parent)
    : QWebEnginePage(p_profile, p_parent) {}

void WebPage::setAllowedMainFrameUrlPredicate(std::function<bool(const QUrl &)> p_predicate) {
  m_allowedMainFrameUrlPredicate = std::move(p_predicate);
}

void WebPage::setProtectedDocumentUrl(const QUrl &p_url) {
  m_protectedRoot.reset(new QUrl(p_url));
  m_protectedRootPending = true;
}

bool WebPage::acceptNavigationRequest(const QUrl &p_url, NavigationType p_type,
                                      bool p_isMainFrame) {
  if (m_protectedRoot) {
    if (!p_isMainFrame) {
      return false;
    }
    if (m_protectedRootPending && p_url == *m_protectedRoot &&
        p_type != QWebEnginePage::NavigationTypeLinkClicked) {
      m_protectedRootPending = false;
      return true;
    }
    if (p_type == QWebEnginePage::NavigationTypeLinkClicked) {
      const auto scheme = p_url.scheme();
      if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        emit externalLinkRequested(p_url);
      }
    }
    return false;
  }
  Q_UNUSED(p_type);
  // Checked before the isLocalFile() branch so a consumer-owned scheme can never
  // be mistaken for a user-authored link. No allowlisted scheme is a local file
  // today, so this does not change any existing behaviour.
  if (p_isMainFrame && m_allowedMainFrameUrlPredicate && m_allowedMainFrameUrlPredicate(p_url)) {
    return true;
  }
  if (p_url.isLocalFile()) {
    emit localFileOpenRequested(p_url);
    return false;
  }
  if (!p_isMainFrame) {
    return true;
  }

  const auto scheme = p_url.scheme();
  if (scheme == QStringLiteral("data")) {
    // Qt 5.12 and above will trigger this when calling QWebEngineView::setHtml().
    return true;
  } else if (scheme == QStringLiteral("chrome-devtools") || scheme == QStringLiteral("devtools")) {
    return true;
  }

  emit externalLinkRequested(p_url);
  return false;
}

void WebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                       int lineNumber, const QString &sourceID) {
  if (m_protectedRoot || m_sensitiveContent) {
    // Renderer errors can quote note source. Do not forward even warnings to Qt.
    return;
  }
  if (level == QWebEnginePage::InfoMessageLevel) {
    qCDebug(lcWebJs) << "JS(" << sourceID << ":" << lineNumber << "):" << message;
  }
  QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceID);
}
