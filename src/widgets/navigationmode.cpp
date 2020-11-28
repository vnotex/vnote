#include "navigationmode.h"

#include <QLabel>
#include <QListWidgetItem>
#include <QListWidget>
#include <QTreeWidgetItem>
#include <QTreeWidget>
#include <QScrollBar>
#include <QFont>
#include <QFontMetrics>

#include "treewidget.h"
#include <utils/widgetutils.h>
#include <core/vnotex.h>
#include <core/thememgr.h>

using namespace vnotex;

NavigationMode::NavigationMode(Type p_type, QWidget *p_widget)
    : m_type(p_type),
      m_widget(p_widget)
{
}

void NavigationMode::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
}

void NavigationMode::hideNavigation()
{
    clearNavigation();
}

void NavigationMode::clearNavigation()
{
    m_secondKeyMap.clear();
    for (auto label : m_navigationLabels) {
        delete label;
    }
    m_navigationLabels.clear();

    m_isMajorKeyConsumed = false;
}

QChar NavigationMode::generateSecondKey(int p_idx)
{
    if (p_idx < 0 || p_idx >= c_maxNumOfNavigationItems) {
        return QChar();
    }

    if (p_idx < 26) {
        return QChar('a' + p_idx);
    } else {
        return QChar('0' + p_idx - 26);
    }
}

QString NavigationMode::generateLabelString(QChar p_secondKey) const
{
    return p_secondKey.isNull() ? QString(m_majorKey) : QString(m_majorKey) + p_secondKey;
}

static QString generateNavigationLabelStyle(const QString &p_str, bool p_tiny)
{
    static int lastLen = -1;
    static int pxWidth = 24;
    static int pxHeight = 24;
    const int fontPt = p_tiny ? 12 : 15;

    auto fontFamily = WidgetUtils::getMonospaceFont();

    if (p_str.size() != lastLen) {
        QFont font(fontFamily, fontPt);
        font.setBold(true);
        QFontMetrics fm(font);
        pxWidth = fm.width(p_str);
        pxHeight = fm.capHeight() + 5;
        lastLen = p_str.size();
    }

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    QColor bg(themeMgr.paletteColor(QStringLiteral("widgets#navigationlabel#bg")));
    bg.setAlpha(200);

    QString style = QString("background-color: %1;"
                            "color: %2;"
                            "font-size: %3pt;"
                            "font: bold;"
                            "font-family: %4;"
                            "border-radius: 3px;"
                            "min-width: %5px;"
                            "max-width: %5px;")
                           .arg(bg.name(QColor::HexArgb))
                           .arg(themeMgr.paletteColor(QStringLiteral("widgets#navigationlabel#fg")))
                           .arg(fontPt)
                           .arg(fontFamily)
                           .arg(pxWidth);

    if (p_tiny) {
        style += QString("margin: 0px;"
                         "padding: 0px;"
                         "min-height: %1px;"
                         "max-height: %1px;").arg(pxHeight);
    }

    return style;
}

QLabel *NavigationMode::createNavigationLabel(QChar p_secondKey, QWidget *p_parent) const
{
    QString labelStr = generateLabelString(p_secondKey);
    QLabel *label = new QLabel(labelStr, p_parent);
    label->setStyleSheet(generateNavigationLabelStyle(labelStr, false));
    return label;
}

NavigationMode::Status NavigationMode::handleKeyNavigation(int p_key)
{
    Status sta;
    auto keyChar = Utils::keyToChar(p_key, true);
    if (m_isMajorKeyConsumed && !keyChar.isNull()) {
        m_isMajorKeyConsumed = false;
        sta.m_isTargetHit = true;
        auto it = m_secondKeyMap.find(keyChar);
        if (it != m_secondKeyMap.end()) {
            sta.m_isKeyConsumed = true;
            handleTargetHit(it.value());
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        sta.m_isKeyConsumed = true;
        switch (m_type) {
        case Type::SingleKey:
            sta.m_isTargetHit = true;
            handleTargetHit(nullptr);
            break;

        case Type::StagedDoubleKeys:
            // Hit the major stage.
            handleTargetHit(nullptr);

            // Show the second stage.
            showNavigationWithDoubleKeys();

            Q_FALLTHROUGH();

        case Type::DoubleKeys:
            if (m_secondKeyMap.isEmpty()) {
                sta.m_isTargetHit = true;
            } else {
                m_isMajorKeyConsumed = true;
            }
            break;
        }
    }

    return sta;
}

void NavigationMode::showNavigation()
{
    clearNavigation();

    if (!isTargetVisible()) {
        return;
    }

    switch (m_type) {
    case Type::SingleKey:
        Q_FALLTHROUGH();
    case Type::StagedDoubleKeys:
    {
        auto label = createNavigationLabel(QChar(), m_widget);
        placeNavigationLabel(-1, nullptr, label);
        label->show();
        m_navigationLabels.append(label);
        break;
    }

    case Type::DoubleKeys:
    {
        showNavigationWithDoubleKeys();
        break;
    }
    }
}

bool NavigationMode::isTargetVisible()
{
    return m_widget->isVisible();
}

QVector<void *> NavigationMode::getVisibleNavigationItems()
{
    return QVector<void *>();
}

void NavigationMode::showNavigationWithDoubleKeys()
{
    // Generate labels for visible items.
    auto items = getVisibleNavigationItems();
    for (int i = 0; i < items.size(); ++i) {
        const auto key = generateSecondKey(i);
        if (key.isNull()) {
            break;
        }

        m_secondKeyMap[key] = items[i];

        auto label = createNavigationLabel(key, m_widget);
        placeNavigationLabel(i, items[i], label);
        label->show();

        m_navigationLabels.append(label);
    }
}
