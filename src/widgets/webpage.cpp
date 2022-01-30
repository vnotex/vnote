#include "webpage.h"

#include <utils/widgetutils.h>
#include <core/vnotex.h>
#include <core/fileopenparameters.h>

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
    if (p_url.isLocalFile()) {
        emit VNoteX::getInst().openFileRequested(p_url.toLocalFile(),
                                                 QSharedPointer<FileOpenParameters>::create());
        return false;
    } if (!p_isMainFrame) {
        return true;
    }

    const auto scheme = p_url.scheme();
    if (scheme == QStringLiteral("data")) {
        // Qt 5.12 and above will trigger this when calling QWebEngineView::setHtml().
        return true;
    } else if (scheme == QStringLiteral("chrome-devtools") || scheme == QStringLiteral("devtools")) {
        return true;
    }

    WidgetUtils::openUrlByDesktop(p_url);
    return false;
}
