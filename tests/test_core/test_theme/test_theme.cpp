#include "test_theme.h"

#include <QHash>

#include <theme.h>

using namespace tests;

using namespace vnotex;

TestTheme::TestTheme(QObject *p_parent)
    : QObject(p_parent)
{

}

void TestTheme::checkKeyValue(const QJsonObject &p_obj,
                              const QString &p_key,
                              const QString &p_val)
{
    auto val = Theme::findValueByKeyPath(p_obj, p_key);
    QCOMPARE(val.toString(), p_val);
}

void TestTheme::testTranslatePaletteObject()
{
    QJsonObject palette;

    // Normal.
    {
        QJsonObject base;
        base["a"] = "green";
        base["b"] = "blue";
        palette["base"] = base;

        auto ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, false);
        QCOMPARE(ret.second, 0);

        checkKeyValue(palette, "base#a", "green");
        checkKeyValue(palette, "base#b", "blue");
    }

    // @.
    {
        auto baseObj = palette["base"].toObject();
        baseObj["c"] = "@base#b";
        palette["base"] = baseObj;

        auto ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, true);
        QCOMPARE(ret.second, 0);
        checkKeyValue(palette, "base#a", "green");
        checkKeyValue(palette, "base#b", "blue");
        checkKeyValue(palette, "base#c", "blue");
    }

    // @&@.
    {
        auto baseObj = palette["base"].toObject();
        baseObj["e"] = "@base#d";
        baseObj["d"] = "@base#a";
        palette["base"] = baseObj;

        auto ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, true);
        QCOMPARE(ret.second, 1);

        ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, true);
        QCOMPARE(ret.second, 0);

        checkKeyValue(palette, "base#d", "green");
        checkKeyValue(palette, "base#e", "green");
    }

    // Cyclic @.
    {
        auto baseObj = palette["base"].toObject();
        baseObj["f"] = "@base#g";
        baseObj["g"] = "@base#h";
        baseObj["h"] = "@base#f";
        palette["base"] = baseObj;

        auto ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, true);
        QCOMPARE(ret.second, 3);

        ret = Theme::translatePaletteObjectOnce(palette, palette, "base");
        QCOMPARE(ret.first, true);
        QCOMPARE(ret.second, 3);

        checkKeyValue(palette, "base#a", "green");
        checkKeyValue(palette, "base#b", "blue");
        checkKeyValue(palette, "base#c", "blue");
        checkKeyValue(palette, "base#d", "green");
        checkKeyValue(palette, "base#e", "green");

        QVERIFY(Theme::isRef(Theme::findValueByKeyPath(palette, "base#f").toString()));
        QVERIFY(Theme::isRef(Theme::findValueByKeyPath(palette, "base#g").toString()));
        QVERIFY(Theme::isRef(Theme::findValueByKeyPath(palette, "base#h").toString()));
    }
}

void TestTheme::testTranslateStyleByPalette()
{
    QJsonObject palette;

    {
        QJsonObject baseObj;
        baseObj["a"] = "green";
        baseObj["b"] = "blue";
        baseObj["c"] = "red";
        palette["base"] = baseObj;
    }

    // Space or column prefix.
    {
        QString style = "color: @base#a;\n background:@base#b;";
        Theme::translateStyleByPalette(palette, style);
        QVERIFY(style == "color: green;\n background:blue;");
    }

    // Reference beyond palette.
    {
        QString style = "color: @base#a;\n background: @base#d;";
        Theme::translateStyleByPalette(palette, style);
        QVERIFY(style == "color: green;\n background: @base#d;");
    }
}

void TestTheme::testTranslateUrlToAbsolute()
{
#if defined(Q_OS_WIN)
    const QString basePath("C:\\vnotex\\theme");

    QString style = "image: url(menu.svg); image:url(c:/vnotex/abs.svg);";
    Theme::translateUrlToAbsolute(basePath, style);
    QVERIFY(style == "image: url(C:/vnotex/theme/menu.svg); image:url(c:/vnotex/abs.svg);");
#else
    const QString basePath("/usr/bin/vnotex/theme");
    QString style = "image: url(menu.svg); image:url(/usr/bin/abs.svg);";
    Theme::translateUrlToAbsolute(basePath, style);
    QVERIFY(style == "image: url(/usr/bin/vnotex/theme/menu.svg); image:url(/usr/bin/abs.svg);");
#endif
}

void TestTheme::testTranslateScaledSize()
{
    const qreal factor = 1.5;

    {
    QString style = "width: $20px; height:$-4px;";
    Theme::translateScaledSize(factor, style);
    QVERIFY(style == "width: 30px; height:-6px;");
    }
}

QTEST_MAIN(tests::TestTheme)
