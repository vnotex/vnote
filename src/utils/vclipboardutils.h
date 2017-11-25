#ifndef VCLIPBOARDUTILS_H
#define VCLIPBOARDUTILS_H

#include <QImage>
#include <QClipboard>


class VClipboardUtils
{
public:
    static void setImageToClipboard(QClipboard *p_clipboard,
                                    const QImage &p_image,
                                    QClipboard::Mode p_mode = QClipboard::Clipboard);

private:
    VClipboardUtils()
    {
    }

    static void setImageLoop(QClipboard *p_clipboard,
                             const QImage &p_image,
                             QClipboard::Mode p_mode);
};

#endif // VCLIPBOARDUTILS_H
