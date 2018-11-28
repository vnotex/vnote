#include "vinserttabledialog.h"

#include <QSpinBox>
#include <QRadioButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QButtonGroup>

VInsertTableDialog::VInsertTableDialog(QWidget *p_parent)
    : QDialog(p_parent),
      m_alignment(VTable::None)
{
    setupUI();
}

void VInsertTableDialog::setupUI()
{
    m_rowCount = new QSpinBox(this);
    m_rowCount->setToolTip(tr("Number of rows of the table body"));
    m_rowCount->setMaximum(1000);
    m_rowCount->setMinimum(0);

    m_colCount = new QSpinBox(this);
    m_colCount->setToolTip(tr("Number of columns of the table"));
    m_colCount->setMaximum(1000);
    m_colCount->setMinimum(1);

    QRadioButton *noneBtn = new QRadioButton(tr("None"), this);
    QRadioButton *leftBtn = new QRadioButton(tr("Left"), this);
    QRadioButton *centerBtn = new QRadioButton(tr("Center"), this);
    QRadioButton *rightBtn = new QRadioButton(tr("Right"), this);
    QHBoxLayout *alignLayout = new QHBoxLayout();
    alignLayout->addWidget(noneBtn);
    alignLayout->addWidget(leftBtn);
    alignLayout->addWidget(centerBtn);
    alignLayout->addWidget(rightBtn);
    alignLayout->addStretch();

    noneBtn->setChecked(true);

    QButtonGroup *bg = new QButtonGroup(this);
    bg->addButton(noneBtn, VTable::None);
    bg->addButton(leftBtn, VTable::Left);
    bg->addButton(centerBtn, VTable::Center);
    bg->addButton(rightBtn, VTable::Right);
    connect(bg, static_cast<void(QButtonGroup::*)(int, bool)>(&QButtonGroup::buttonToggled),
            this, [this](int p_id, bool p_checked){
                if (p_checked) {
                    m_alignment = static_cast<VTable::Alignment>(p_id);
                }
            });

    QGridLayout *topLayout = new QGridLayout();
    topLayout->addWidget(new QLabel(tr("Row:")), 0, 0, 1, 1);
    topLayout->addWidget(m_rowCount, 0, 1, 1, 1);
    topLayout->addWidget(new QLabel(tr("Column:")), 0, 2, 1, 1);
    topLayout->addWidget(m_colCount, 0, 3, 1, 1);

    topLayout->addWidget(new QLabel(tr("Alignment:")), 1, 0, 1, 1);
    topLayout->addLayout(alignLayout, 1, 1, 1, 3);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    setWindowTitle(tr("Insert Table"));
}

int VInsertTableDialog::getRowCount() const
{
    return m_rowCount->value();
}

int VInsertTableDialog::getColumnCount() const
{
    return m_colCount->value();
}

VTable::Alignment VInsertTableDialog::getAlignment() const
{
    return m_alignment;
}
