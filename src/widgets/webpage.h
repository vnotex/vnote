#ifndef WEBPAGE_H
#define WEBPAGE_H

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

signals:
  void localFileOpenRequested(const QUrl &p_url);

  void externalLinkRequested(const QUrl &p_url);

protected:
  bool acceptNavigationRequest(const QUrl &p_url, NavigationType p_type,
                               bool p_isMainFrame) Q_DECL_OVERRIDE;

  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                int lineNumber, const QString &sourceID) override;
};
} // namespace vnotex

#endif // WEBPAGE_H
