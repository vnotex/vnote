#include "vpreviewpage.h"

#include <QDesktopServices>

#include "vnote.h"
#include "vmainwindow.h"

extern VNote *g_vnote;

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
        if (g_vnote->getMainWindow()->tryOpenInternalFile(filePath)) {
            qDebug() << "internal notes jump" << filePath;
            return false;
        }
    }

    QDesktopServices::openUrl(p_url);
    return false;
}
