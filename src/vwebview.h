#ifndef VWEBVIEW_H
#define VWEBVIEW_H

#include <QWebEngineView>

class VFile;

class VWebView : public QWebEngineView
{
    Q_OBJECT
public:
    // @p_file could be NULL.
    explicit VWebView(VFile *p_file, QWidget *p_parent = Q_NULLPTR);

signals:
    void editNote();

protected:
    void contextMenuEvent(QContextMenuEvent *p_event);

private slots:
    void handleEditAction();

    // Copy the clicked image.
    // Used to replace the default CopyImageToClipboard action.
    void copyImage();

private:
    VFile *m_file;

    // Whether this view has hooked the Copy Image Url action.
    bool m_actionHooked;
};

#endif // VWEBVIEW_H
