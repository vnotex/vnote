#include "clipboardutils.h"

#include <QString>
#include <QStringLiteral>
#include <QApplication>
#include <QMimeData>
#include <QDebug>
#include <QUrl>
#include <QAction>

#include "utils.h"

using namespace vnotex;

QString ClipboardUtils::getTextFromClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString subtype("plain");
    return clipboard->text(subtype);
}

void ClipboardUtils::setTextToClipboard(const QString &p_text)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(p_text);
}

void ClipboardUtils::clearClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(QString());
}

void ClipboardUtils::setMimeDataToClipboard(QClipboard *p_clipboard,
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

bool ClipboardUtils::mimeDataEquals(const QMimeData *p_a, const QMimeData *p_b)
{
    if (!p_a && !p_b) {
        return true;
    }

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

void ClipboardUtils::setMimeDataLoop(QClipboard *p_clipboard,
                                     QMimeData *p_mimeData,
                                     QClipboard::Mode p_mode)
{
    for (int i = 0; i < 100; ++i) {
        // Make a backup.
        auto tmp = cloneMimeData(p_mimeData);

        p_clipboard->setMimeData(p_mimeData, p_mode);
        const QMimeData *out = p_clipboard->mimeData(p_mode);
        if (mimeDataEquals(tmp.get(), out)) {
            return;
        }

        qDebug() << "failed to set mimeData, retry" << i;
        p_mimeData = tmp.release();
        Utils::sleepWait(100 /* ms */);
    }

    delete p_mimeData;
}

std::unique_ptr<QMimeData> ClipboardUtils::cloneMimeData(const QMimeData *p_mimeData)
{
    std::unique_ptr<QMimeData> da(new QMimeData());
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

void ClipboardUtils::setImageToClipboard(QClipboard *p_clipboard,
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

void ClipboardUtils::setImageLoop(QClipboard *p_clipboard,
                                  const QImage &p_image,
                                  QClipboard::Mode p_mode)
{
    for (int i = 0; i < 100; ++i) {
        p_clipboard->setImage(p_image, p_mode);

        QImage image = p_clipboard->image(p_mode);
        if (!image.isNull()) {
            break;
        }

        qWarning() << "failed to set image, retry" << i;
        Utils::sleepWait(100 /* ms */);
    }
}
