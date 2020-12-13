#include "utils.h"

#include <QElapsedTimer>
#include <QDir>
#include <QKeySequence>
#include <QWidget>
#include <QVariant>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QPainter>

#include <cmath>

using namespace vnotex;

void Utils::sleepWait(int p_milliseconds)
{
    if (p_milliseconds <= 0) {
        return;
    }

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < p_milliseconds) {
        QCoreApplication::processEvents();
    }
}

void Utils::appendMsg(QString &p_msg, const QString &p_new)
{
    if (p_msg.isEmpty()) {
        p_msg = p_new;
    } else {
        p_msg += '\n' + p_new;
    }
}

QString Utils::dateTimeString(const QDateTime &p_dateTime)
{
    return p_dateTime.date().toString(Qt::DefaultLocaleLongDate)
           + " "
           + p_dateTime.time().toString(Qt::TextDate);
}

QString Utils::dateTimeStringUniform(const QDateTime &p_dateTime)
{
    return p_dateTime.toString(Qt::ISODate);
}

QDateTime Utils::dateTimeFromStringUniform(const QString &p_str)
{
    return QDateTime::fromString(p_str, Qt::ISODate);
}

QChar Utils::keyToChar(int p_key, bool p_lowerCase)
{
    auto keyStr = QKeySequence(p_key).toString();
    if (keyStr.size() == 1) {
        return p_lowerCase ? keyStr[0].toLower() : keyStr[0];
    }

    return QChar();
}

QString Utils::pickAvailableFontFamily(const QStringList &p_families)
{
    auto availableFonts = QFontDatabase().families();
    for (const auto& f : p_families) {
        auto family = f.trimmed();
        if (family.isEmpty()) {
            continue;
        }

        for (auto availableFont : availableFonts) {
            availableFont.remove(QRegularExpression("\\[.*\\]"));
            availableFont = availableFont.trimmed();
            if (family == availableFont
                || family.toLower() == availableFont.toLower()) {
                return availableFont;
            }
        }
    }

    return QString();
}

QPixmap Utils::svgToPixmap(const QByteArray &p_content,
                           QRgb p_background,
                           qreal p_scaleFactor)
{
    QSvgRenderer renderer(p_content);
    QSize deSz = renderer.defaultSize();
    if (p_scaleFactor > 0) {
        deSz *= p_scaleFactor;
    }

    QPixmap pm(deSz);
    if (p_background == 0x0) {
        // Fill a transparent background to avoid glitchy preview.
        pm.fill(QColor(255, 255, 255, 0));
    } else {
        pm.fill(p_background);
    }

    QPainter painter(&pm);
    renderer.render(&painter);
    return pm;
}

bool Utils::fuzzyEqual(qreal p_a, qreal p_b)
{
    return std::abs(p_a - p_b) < std::pow(10, -6);
}

QString Utils::boolToString(bool p_val)
{
    return p_val ? QStringLiteral("true") : QStringLiteral("false");
}
