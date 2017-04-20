#ifndef VWEBVIEW_H
#define VWEBVIEW_H

#include <QWebEngineView>

class VFile;

class VWebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit VWebView(VFile *p_file, QWidget *p_parent = Q_NULLPTR);

signals:
    void editNote();

protected:
    void contextMenuEvent(QContextMenuEvent *p_event);

private slots:
    void handleEditAction();

private:
    VFile *m_file;
};

#endif // VWEBVIEW_H
