#ifndef WIDGETSFACTORY_H
#define WIDGETSFACTORY_H

class QMenu;
class QWidget;
class QLineEdit;
class QString;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QDoubleSpinBox;

namespace vnotex
{
    class WidgetsFactory
    {
    public:
        WidgetsFactory() = delete;

        static QMenu *createMenu(QWidget *p_parent = nullptr);

        static QMenu *createMenu(const QString &p_title, QWidget *p_parent = nullptr);

        static QLineEdit *createLineEdit(QWidget *p_parent = nullptr);

        static QLineEdit *createLineEdit(const QString &p_contents, QWidget *p_parent = nullptr);

        static QComboBox *createComboBox(QWidget *p_parent = nullptr);

        static QCheckBox *createCheckBox(const QString &p_text, QWidget *p_parent = nullptr);

        static QSpinBox *createSpinBox(QWidget *p_parent = nullptr);

        static QDoubleSpinBox *createDoubleSpinBox(QWidget *p_parent = nullptr);

        static QToolButton *createToolButton(QWidget *p_parent = nullptr);
    };
} // ns vnotex

#endif // WIDGETSFACTORY_H
