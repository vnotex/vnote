#include "webpage.h"

using namespace vnotex;

WebPage::WebPage(QWidget *p_parent)
    : QWebEnginePage(p_parent)
{

}

bool WebPage::acceptNavigationRequest(const QUrl &p_url,
                                      NavigationType p_type,
                                      bool p_isMainFrame)
{
    Q_UNUSED(p_type);
    if (!p_isMainFrame) {
        return true;
    } else if (p_url.scheme() == QStringLiteral("data")) {
        // Qt 5.12 and above will trigger this when calling QWebEngineView::setHtml().
        return true;
    }

    return false;
}
