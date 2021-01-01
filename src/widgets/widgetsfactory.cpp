#include "widgetsfactory.h"

#include <QMenu>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QToolButton>

#include "lineedit.h"
#include "combobox.h"

using namespace vnotex;

QMenu *WidgetsFactory::createMenu(QWidget *p_parent)
{
    auto menu = new QMenu(p_parent);
    menu->setToolTipsVisible(true);
    return menu;
}

QMenu *WidgetsFactory::createMenu(const QString &p_title, QWidget *p_parent)
{
    auto menu = new QMenu(p_title, p_parent);
    menu->setToolTipsVisible(true);
    return menu;
}

QLineEdit *WidgetsFactory::createLineEdit(QWidget *p_parent)
{
    return new LineEdit(p_parent);
}

QLineEdit *WidgetsFactory::createLineEdit(const QString &p_contents, QWidget *p_parent)
{
    return new LineEdit(p_contents, p_parent);
}

QComboBox *WidgetsFactory::createComboBox(QWidget *p_parent)
{
    auto comboBox = new ComboBox(p_parent);
    auto itemDelegate = new QStyledItemDelegate(comboBox);
    comboBox->setItemDelegate(itemDelegate);
    return comboBox;
}

QCheckBox *WidgetsFactory::createCheckBox(const QString &p_text, QWidget *p_parent)
{
    return new QCheckBox(p_text, p_parent);
}

QSpinBox *WidgetsFactory::createSpinBox(QWidget *p_parent)
{
    return new QSpinBox(p_parent);
}

QDoubleSpinBox *WidgetsFactory::createDoubleSpinBox(QWidget *p_parent)
{
    return new QDoubleSpinBox(p_parent);
}

QToolButton *WidgetsFactory::createToolButton(QWidget *p_parent)
{
    auto tb = new QToolButton(p_parent);
    tb->setPopupMode(QToolButton::MenuButtonPopup);
    return tb;
}
