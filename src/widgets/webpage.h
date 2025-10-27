#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <QWebEnginePage>

namespace vnotex {
class WebPage : public QWebEnginePage {
  Q_OBJECT
public:
  explicit WebPage(QWidget *p_parent = nullptr);

protected:
  bool acceptNavigationRequest(const QUrl &p_url, NavigationType p_type,
                               bool p_isMainFrame) Q_DECL_OVERRIDE;

  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                int lineNumber, const QString &sourceID) override;
};
} // namespace vnotex

#endif // WEBPAGE_H
