#include "dialog.h"

#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>
#include <QDesktopWidget>
#include <QScrollBar>
#include <QTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCoreApplication>

#include <utils/widgetutils.h>
#include "../propertydefs.h"

using namespace vnotex;

Dialog::Dialog(QWidget *p_parent, Qt::WindowFlags p_flags)
    : QDialog(p_parent, p_flags)
{
    m_layout = new QVBoxLayout(this);
}

void Dialog::setCentralWidget(QWidget *p_widget)
{
    Q_ASSERT(!m_centralWidget && p_widget);
    m_centralWidget = p_widget;
    m_centralWidget->setProperty(PropertyDefs::s_dialogCentralWidget, true);
    m_layout->addWidget(m_centralWidget);
}

void Dialog::setDialogButtonBox(QDialogButtonBox::StandardButtons p_buttons,
                                QDialogButtonBox::StandardButton p_defaultButton)
{
    if (m_dialogButtonBox) {
        m_dialogButtonBox->setStandardButtons(p_buttons);
    } else {
        m_dialogButtonBox = new QDialogButtonBox(p_buttons, this);
        connect(m_dialogButtonBox, &QDialogButtonBox::accepted,
                this, &Dialog::acceptedButtonClicked);
        connect(m_dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_dialogButtonBox, &QDialogButtonBox::clicked,
                this, [this](QAbstractButton *p_button) {
                    switch (m_dialogButtonBox->buttonRole(p_button)) {
                    case QDialogButtonBox::ResetRole:
                        resetButtonClicked();
                        break;

                    case QDialogButtonBox::ApplyRole:
                        appliedButtonClicked();
                        break;

                    default:
                        break;
                    }
                });

        m_layout->addWidget(m_dialogButtonBox);
    }

    // If default button is not set, the first button with the accept role is made
    // the default button when the dialog is shown.
    if (p_defaultButton != QDialogButtonBox::NoButton) {
        auto btn = m_dialogButtonBox->button(p_defaultButton);
        if (btn) {
            btn->setDefault(true);
        }
    }
}

QDialogButtonBox *Dialog::getDialogButtonBox() const
{
    return m_dialogButtonBox;
}

void Dialog::setInformationText(const QString &p_text, InformationLevel p_level)
{
    if (!m_infoTextEdit) {
        if (p_text.isEmpty()) {
            return;
        }

        m_infoTextEdit = new QPlainTextEdit(this);
        m_infoTextEdit->setReadOnly(true);
        m_infoTextEdit->setMaximumHeight(m_infoTextEdit->minimumSizeHint().height());
        m_layout->insertWidget(1, m_infoTextEdit);
    }

    m_infoTextEdit->setPlainText(p_text);

    const bool visible = !p_text.isEmpty();
    const bool needResize = visible != m_infoTextEdit->isVisible();
    m_infoTextEdit->setVisible(visible);

    // Change the style.
    const char *level = "";
    switch (p_level) {
    case InformationLevel::Info:
        level = "info";
        break;

    case InformationLevel::Warning:
        level = "warning";
        break;

    case InformationLevel::Error:
        level = "error";
        break;
    }

    WidgetUtils::setPropertyDynamically(m_infoTextEdit, PropertyDefs::s_state, level);
    if (needResize) {
        WidgetUtils::updateSize(this);
    }
}

void Dialog::acceptedButtonClicked()
{
    QDialog::accept();
}

void Dialog::resetButtonClicked()
{
}

void Dialog::appliedButtonClicked()
{
}

void Dialog::setButtonEnabled(QDialogButtonBox::StandardButton p_button, bool p_enabled)
{
    QPushButton *button = getDialogButtonBox()->button(p_button);
    if (button) {
        button->setEnabled(p_enabled);
    }
}

void Dialog::completeButStay()
{
    Q_ASSERT(m_centralWidget);
    m_centralWidget->setEnabled(false);
    m_completed = true;
}

bool Dialog::isCompleted() const
{
    return m_completed;
}
