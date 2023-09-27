#include "utils.h"

#include <QElapsedTimer>
#include <QDir>
#include <QKeySequence>
#include <QWidget>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QLocale>

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
    QLocale locale;
    return locale.toString(p_dateTime.date())
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
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    auto availableFonts = QFontDatabase().families();
#else
    auto availableFonts = QFontDatabase::families();
#endif

    for (const auto& f : p_families) {
        auto family = f.trimmed();
        if (family.isEmpty()) {
            continue;
        }

        QRegularExpression regExp("\\[.*\\]");
        for (auto availableFont : availableFonts) {
            availableFont.remove(regExp);
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

QString Utils::intToString(int p_val, int p_width)
{
    auto str = QString::number(p_val);
    if (str.size() < p_width) {
        str.prepend(QString(p_width - str.size(), QLatin1Char('0')));
    }
    return str;
}

QByteArray Utils::toJsonString(const QJsonObject &p_obj)
{
    QJsonDocument doc(p_obj);
    return doc.toJson(QJsonDocument::Compact);
}

QJsonObject Utils::fromJsonString(const QByteArray &p_data)
{
    return QJsonDocument::fromJson(p_data).object();
}

QJsonValue Utils::parseAndReadJson(const QJsonObject &p_obj, const QString &p_exp)
{
    // abc[0] or abc.
    QRegularExpression regExp(R"(^([^\[\]\s]+)(?:\[(\d+)\])?$)");

    QJsonValue val(p_obj);

    bool valid = true;
    const auto tokens = p_exp.split(QLatin1Char('.'));
    for (int i = 0; i < tokens.size(); ++i) {
        const auto &token = tokens[i];
        if (token.isEmpty()) {
            continue;
        }

        auto match = regExp.match(token);
        if (!match.hasMatch()) {
            valid = false;
            break;
        }

        const auto key = match.captured(1);
        const auto obj = val.toObject();
        if (obj.contains(key)) {
            val = obj.value(key);
        } else {
            valid = false;
            break;
        }

        if (!match.captured(2).isEmpty()) {
            // Array.
            const auto arr = val.toArray();
            int idx = match.captured(2).toInt();
            if (idx < 0 || idx >= arr.size()) {
                valid = false;
                break;
            }

            val = arr[idx];
        }
    }

    if (!valid) {
        qWarning() << "invalid expression to parse for JSON" << p_exp;
        return QJsonValue();
    }

    return val;
}

QColor Utils::toColor(const QString &p_color)
{
    // rgb(123, 123, 123).
    QRegularExpression rgbTripleRegExp(R"(^rgb\((\d+)\s*,\s*(\d+)\s*,\s*(\d+)\)$)", QRegularExpression::CaseInsensitiveOption);
    auto match = rgbTripleRegExp.match(p_color);
    if (match.hasMatch()) {
        return QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
    }

    return QColor(p_color);
}

QStringList Utils::toLower(const QStringList &p_list)
{
    QStringList lowerList;
    for (const auto &ele : p_list) {
        lowerList << ele.toLower();
    }
    return lowerList;
}
