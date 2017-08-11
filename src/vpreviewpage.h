#ifndef VPREVIEWPAGE_H
#define VPREVIEWPAGE_H

#include <QWebEnginePage>

class VPreviewPage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit VPreviewPage(QWidget *parent = 0);

protected:
    bool acceptNavigationRequest(const QUrl &p_url,
                                 NavigationType p_type,
                                 bool p_isMainFrame);
};

#endif // VPREVIEWPAGE_H
