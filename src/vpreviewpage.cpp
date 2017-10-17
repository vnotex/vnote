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
    Q_UNUSED(p_isMainFrame);

    if (p_url.isLocalFile()) {
        QString filePath = p_url.toLocalFile();
        if (g_mainWin->tryOpenInternalFile(filePath)) {
            qDebug() << "internal notes jump" << filePath;
            return false;
        }
    }

    QDesktopServices::openUrl(p_url);
    return false;
}
