#ifndef TEST_THEME_H
#define TEST_THEME_H

#include <QtTest>

namespace vnotex
{
    class IVersionControllerFactory;
    class INotebookConfigMgrFactory;
    class INotebookBackendFactory;
    class INotebookFactory;
}

namespace tests
{
    class TestTheme : public QObject
    {
        Q_OBJECT
    public:
        explicit TestTheme(QObject *p_parent = nullptr);

    private slots:
        // Define test cases here per slot.
        void testTranslatePaletteObject();

        void testTranslateStyleByPalette();

        void testTranslateUrlToAbsolute();

        void testTranslateScaledSize();

    private:
        void checkKeyValue(const QJsonObject &p_obj,
                           const QString &p_key,
                           const QString &p_val);
    };
} // ns tests

#endif // TEST_THEME_H
