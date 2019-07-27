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
    if (p_type == QWebPage::NavigationTypeLinkClicked) {
        auto url = p_request.url();
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            if (g_mainWin->tryOpenInternalFile(filePath)) {
                return false;
            }
        }

        QDesktopServices::openUrl(url);
    }

    return QWebPage::acceptNavigationRequest(p_frame, p_request, p_type);
}

void VPreviewPage::setBackgroundColor(const QColor &p_background)
{
    auto pa = palette();
    pa.setColor(QPalette::Window, p_background);
    pa.setColor(QPalette::Base, p_background);
    setPalette(pa);
}
