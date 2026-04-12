#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <QWebEnginePage>
#include <QWidget>

namespace vnotex {
class WebPage : public QWebEnginePage {
  Q_OBJECT
public:
  explicit WebPage(QWidget *p_parent = nullptr);

signals:
  void localFileOpenRequested(const QString &p_filePath);

protected:
  bool acceptNavigationRequest(const QUrl &p_url, NavigationType p_type,
                               bool p_isMainFrame) Q_DECL_OVERRIDE;

  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                int lineNumber, const QString &sourceID) override;
};
} // namespace vnotex

#endif // WEBPAGE_H
