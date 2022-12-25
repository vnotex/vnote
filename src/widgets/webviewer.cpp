#include "webviewer.h"

#include "webpage.h"

#include <QWebEnginePage>

#include <utils/utils.h>

using namespace vnotex;

WebViewer::WebViewer(const QColor &p_background,
                     qreal p_zoomFactor,
                     QWidget *p_parent)
    : QWebEngineView(p_parent)
{
    setAcceptDrops(false);

    auto viewPage = new WebPage(this);
    setPage(viewPage);

    connect(viewPage, &QWebEnginePage::linkHovered,
            this, &WebViewer::linkHovered);

    // Avoid white flash before loading content.
    // Setting Qt::transparent will force GrayScale antialias rendering.
    if (p_background.isValid()) {
        viewPage->setBackgroundColor(p_background);
    }

    if (!Utils::fuzzyEqual(p_zoomFactor, 1.0)) {
        setZoomFactor(p_zoomFactor);
    }
}

WebViewer::WebViewer(const QColor &p_background, QWidget *p_parent)
    : WebViewer(p_background, 1.0, p_parent)
{
}

WebViewer::~WebViewer()
{
}

void WebViewer::findText(const QString &p_text, FindOptions p_options)
{
    if (p_options & FindOption::RegularExpression) {
        return;
    }

    QWebEnginePage::FindFlags flags;
    if (p_options & FindOption::FindBackward) {
        flags |= QWebEnginePage::FindFlag::FindBackward;
    }
    if (p_options & FindOption::CaseSensitive) {
        flags |= QWebEnginePage::FindFlag::FindCaseSensitively;
    }

    QWebEngineView::findText(p_text, flags);
}
