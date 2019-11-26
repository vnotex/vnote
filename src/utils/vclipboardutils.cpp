#include "vclipboardutils.h"

#include <QDebug>
#include <QMimeData>

#include "vutils.h"

void VClipboardUtils::setImageToClipboard(QClipboard *p_clipboard,
                                          const QImage &p_image,
                                          QClipboard::Mode p_mode)
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // On Windows, setImage() may fail. We will repeatedly retry until succeed.
    // On Linux, QXcbClipboard::setMimeData: Cannot set X11 selection owner.
    setImageLoop(p_clipboard, p_image, p_mode);
#else
    p_clipboard->setImage(p_image, p_mode);
#endif
}

void VClipboardUtils::setImageLoop(QClipboard *p_clipboard,
                                   const QImage &p_image,
                                   QClipboard::Mode p_mode)
{
    for (int i = 0; i < 100; ++i) {
        p_clipboard->setImage(p_image, p_mode);

        QImage image = p_clipboard->image(p_mode);
        if (!image.isNull()) {
            break;
        }

        qWarning() << "fail to set image, retry" << i;

        VUtils::sleepWait(100 /* ms */);
    }
}

void VClipboardUtils::setImageToClipboard(QClipboard *p_clipboard,
                                          const QPixmap &p_image,
                                          QClipboard::Mode p_mode)
{
    QImage img(p_image.toImage());

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // On Windows, setImage() may fail. We will repeatedly retry until succeed.
    // On Linux, QXcbClipboard::setMimeData: Cannot set X11 selection owner.
    setImageLoop(p_clipboard, img, p_mode);
#else
    p_clipboard->setImage(img, p_mode);
#endif
}

void VClipboardUtils::setMimeDataToClipboard(QClipboard *p_clipboard,
                                             QMimeData *p_mimeData,
                                             QClipboard::Mode p_mode)
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // On Windows, setMimeData() may fail. We will repeatedly retry until succeed.
    // On Linux, QXcbClipboard::setMimeData: Cannot set X11 selection owner.
    setMimeDataLoop(p_clipboard, p_mimeData, p_mode);
#else
    p_clipboard->setMimeData(p_mimeData, p_mode);
#endif
}

QMimeData *VClipboardUtils::cloneMimeData(const QMimeData *p_mimeData)
{
    QMimeData *da = new QMimeData();
    if (p_mimeData->hasUrls()) {
        da->setUrls(p_mimeData->urls());
    }

    if (p_mimeData->hasText()) {
        da->setText(p_mimeData->text());
    }

    if (p_mimeData->hasColor()) {
        da->setColorData(p_mimeData->colorData());
    }

    if (p_mimeData->hasHtml()) {
        da->setHtml(p_mimeData->html());
    }

    if (p_mimeData->hasImage()) {
        da->setImageData(p_mimeData->imageData());
    }

    return da;
}

static bool mimeDataEquals(const QMimeData *p_a, const QMimeData *p_b)
{
    if ((p_a && !p_b) || (!p_a && p_b)) {
        return false;
    }

    if (p_a->hasUrls()) {
        if (!p_b->hasUrls()) {
            return false;
        }

        if (p_a->urls() != p_b->urls()) {
            return false;
        }
    } else if (p_b->hasUrls()) {
        return false;
    }

    if (p_a->hasText()) {
        if (!p_b->hasText()) {
            return false;
        }

        if (p_a->text() != p_b->text()) {
            return false;
        }
    } else if (p_b->hasText()) {
        return false;
    }

    if (p_a->hasColor()) {
        if (!p_b->hasColor()) {
            return false;
        }

        if (p_a->colorData() != p_b->colorData()) {
            return false;
        }
    } else if (p_b->hasColor()) {
        return false;
    }

    if (p_a->hasHtml()) {
        if (!p_b->hasHtml()) {
            return false;
        }

        if (p_a->html() != p_b->html()) {
            return false;
        }
    } else if (p_b->hasHtml()) {
        return false;
    }

    if (p_a->hasImage()) {
        if (!p_b->hasImage()) {
            return false;
        }

        if (p_a->imageData() != p_b->imageData()) {
            return false;
        }
    } else if (p_b->hasImage()) {
        return false;
    }

    return true;
}

void VClipboardUtils::setMimeDataLoop(QClipboard *p_clipboard,
                                      QMimeData *p_mimeData,
                                      QClipboard::Mode p_mode)
{
    for (int i = 0; i < 100; ++i) {
        // Make a backup.
        QMimeData *tmp = cloneMimeData(p_mimeData);

        p_clipboard->setMimeData(p_mimeData, p_mode);
        const QMimeData *out = p_clipboard->mimeData(p_mode);
        if (mimeDataEquals(tmp, out)) {
            delete tmp;
            break;
        }

        qDebug() << "fail to set mimeData, retry" << i;
        p_mimeData = tmp;

        VUtils::sleepWait(100 /* ms */);
    }
}

QMimeData *linkMimeData(const QString &p_link)
{
    QList<QUrl> urls;
    urls.append(VUtils::pathToUrl(p_link));
    QMimeData *data = new QMimeData();
    data->setUrls(urls);

    QString text = urls[0].toEncoded();
#if defined(Q_OS_WIN)
    if (urls[0].isLocalFile()) {
        text = urls[0].toString(QUrl::EncodeSpaces);
    }
#endif

    data->setText(text);
    return data;
}

void VClipboardUtils::setLinkToClipboard(QClipboard *p_clipboard,
                                         const QString &p_link,
                                         QClipboard::Mode p_mode)
{
    VClipboardUtils::setMimeDataToClipboard(p_clipboard,
                                            linkMimeData(p_link),
                                            p_mode);
}

void VClipboardUtils::setImageAndLinkToClipboard(QClipboard *p_clipboard,
                                                 const QImage &p_image,
                                                 const QString &p_link,
                                                 QClipboard::Mode p_mode)
{
    QMimeData *data = linkMimeData(p_link);
    data->setImageData(p_image);
    VClipboardUtils::setMimeDataToClipboard(p_clipboard,
                                            data,
                                            p_mode);
}

void VClipboardUtils::setTextToClipboard(QClipboard *p_clipboard,
                                         const QString &p_text,
                                         QClipboard::Mode p_mode)
{
    p_clipboard->setText(p_text, p_mode);
}
