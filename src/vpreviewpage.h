#ifndef VPREVIEWPAGE_H
#define VPREVIEWPAGE_H

#include <QWebPage>
#include <QColor>

class VPreviewPage : public QWebPage
{
    Q_OBJECT
public:
    explicit VPreviewPage(QWidget *parent = 0);

    void setBackgroundColor(const QColor &p_background);

protected:
    bool acceptNavigationRequest(QWebFrame *p_frame,
                                 const QNetworkRequest &p_request,
                                 QWebPage::NavigationType p_type) Q_DECL_OVERRIDE;
};

#endif // VPREVIEWPAGE_H
