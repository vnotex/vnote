#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <functional>
#include <memory>

#include <QWebEnginePage>
#include <QWidget>

class QWebEngineProfile;

namespace vnotex {
class WebPage : public QWebEnginePage {
  Q_OBJECT
public:
  explicit WebPage(QWidget *p_parent = nullptr);

  // Create a page on a specific profile. p_profile must not be null and must outlive the page.
  WebPage(QWebEngineProfile *p_profile, QWidget *p_parent);

  // Opt-in allowlist for main-frame navigations that must be handled IN the page
  // rather than routed out through externalLinkRequested.
  //
  // This page class is shared by PDF, MindMap, Markdown and the Windows warm-up
  // page, so the allowance MUST stay per-consumer: the default (no predicate)
  // keeps every existing consumer's behaviour byte for byte, and only the PDF
  // viewer installs one, for its own `vxpdf://` viewer route.
  void setAllowedMainFrameUrlPredicate(std::function<bool(const QUrl &)> p_predicate);

  // Protected pages permit one explicitly requested root load, never note navigation.
  void setProtectedDocumentUrl(const QUrl &p_url);

signals:
  void localFileOpenRequested(const QUrl &p_url);

  void externalLinkRequested(const QUrl &p_url);

  void protectedResourceOpenRequested(const QUrl &p_url);

protected:
  bool acceptNavigationRequest(const QUrl &p_url, NavigationType p_type,
                               bool p_isMainFrame) Q_DECL_OVERRIDE;

  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                int lineNumber, const QString &sourceID) override;

private:
  std::function<bool(const QUrl &)> m_allowedMainFrameUrlPredicate;
  std::unique_ptr<QUrl> m_protectedRoot;
  bool m_protectedRootPending = false;
};
} // namespace vnotex

#endif // WEBPAGE_H
