#include <QtWidgets>
#include "vselectdialog.h"

VSelectDialog::VSelectDialog(const QString &p_title, QWidget *p_parent)
    : QDialog(p_parent), m_choice(-1)
{
    setupUI(p_title);
}

void VSelectDialog::setupUI(const QString &p_title)
{
    m_mainLayout = new QVBoxLayout();

    QPushButton *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setProperty("SelectionBtn", true);
    connect(cancelBtn, &QPushButton::clicked,
            this, &VSelectDialog::reject);
    m_mainLayout->addWidget(cancelBtn);

    setLayout(m_mainLayout);
    m_mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);
}

void VSelectDialog::addSelection(const QString &p_selectStr, int p_selectID)
{
    Q_ASSERT(p_selectID >= 0);

    QPushButton *btn = new QPushButton(p_selectStr);
    btn->setProperty("SelectionBtn", true);
    if (m_selections.isEmpty()) {
        btn->setDefault(true);
        btn->setAutoDefault(true);
    }
    connect(btn, &QPushButton::clicked,
            this, &VSelectDialog::selectionChosen);
    m_selections.insert(btn, p_selectID);
    m_mainLayout->insertWidget(m_selections.size() - 1, btn);
}

void VSelectDialog::selectionChosen()
{
    QPushButton *btn = dynamic_cast<QPushButton *>(sender());
    Q_ASSERT(btn);
    auto it = m_selections.find(btn);
    Q_ASSERT(it != m_selections.end());
    m_choice = *it;
    accept();
}

int VSelectDialog::getSelection() const
{
    return m_choice;
}
