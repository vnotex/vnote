#include "vpreviewpage.h"

#include <QDesktopServices>

#include "vmainwindow.h"

extern VMainWindow *g_mainWin;

VPreviewPage::VPreviewPage(QWidget *parent) : QWebEnginePage(parent)
{

}

bool VPreviewPage::acceptNavigationRequest(const QUrl &p_url,
                                           QWebEnginePage::NavigationType p_type,
                                           bool p_isMainFrame)
{
    Q_UNUSED(p_type);

    if (p_url.isLocalFile()) {
        QString filePath = p_url.toLocalFile();
        if (g_mainWin->tryOpenInternalFile(filePath)) {
            return false;
        }
    } else if (!p_isMainFrame) {
        return true;
    } else if (p_url.scheme() == "data") {
        // Qt 5.12 will trigger this when calling QWebEngineView.setHtml().
        return true;
    }

    QDesktopServices::openUrl(p_url);
    return false;
}
