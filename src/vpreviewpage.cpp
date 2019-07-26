#include "vpreviewpage.h"

#include <QDesktopServices>
#include <QNetworkRequest>
#include <QWebFrame>

#include "vmainwindow.h"

extern VMainWindow *g_mainWin;

VPreviewPage::VPreviewPage(QWidget *parent)
    : QWebPage(parent)
{

}

bool VPreviewPage::acceptNavigationRequest(QWebFrame *p_frame,
                                           const QNetworkRequest &p_request,
                                           QWebPage::NavigationType p_type)
{
    Q_UNUSED(p_frame);
    Q_UNUSED(p_type);

    auto url = p_request.url();
    if (url.isLocalFile()) {
        QString filePath = url.toLocalFile();
        if (g_mainWin->tryOpenInternalFile(filePath)) {
            return false;
        }
    } else if (p_frame) {
        return true;
    } else if (url.scheme() == "data") {
        // Qt 5.12 will trigger this when calling QWebEngineView.setHtml().
        return true;
    }

    QDesktopServices::openUrl(url);
    return false;
}

void VPreviewPage::setBackgroundColor(const QColor &p_background)
{
    auto pa = palette();
    pa.setColor(QPalette::Window, p_background);
    pa.setColor(QPalette::Base, p_background);
    setPalette(pa);
}
