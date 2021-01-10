#include "tableinsertdialog.h"

#include <QSpinBox>
#include <QRadioButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QButtonGroup>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

TableInsertDialog::TableInsertDialog(const QString &p_title, QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI(p_title);
}

void TableInsertDialog::setupUI(const QString &p_title)
{
    auto mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    auto mainLayout = new QGridLayout(mainWidget);

    m_rowCountSpinBox = WidgetsFactory::createSpinBox(mainWidget);
    m_rowCountSpinBox->setToolTip(tr("Row count of the table body"));
    m_rowCountSpinBox->setMaximum(1000);
    m_rowCountSpinBox->setMinimum(0);

    mainLayout->addWidget(new QLabel(tr("Row:")), 0, 0, 1, 1);
    mainLayout->addWidget(m_rowCountSpinBox, 0, 1, 1, 1);

    m_colCountSpinBox = WidgetsFactory::createSpinBox(mainWidget);
    m_colCountSpinBox->setToolTip(tr("Column count of the table"));
    m_colCountSpinBox->setMaximum(1000);
    m_colCountSpinBox->setMinimum(1);

    mainLayout->addWidget(new QLabel(tr("Column:")), 0, 2, 1, 1);
    mainLayout->addWidget(m_colCountSpinBox, 0, 3, 1, 1);

    {
        auto noneBtn = new QRadioButton(tr("None"), mainWidget);
        auto leftBtn = new QRadioButton(tr("Left"), mainWidget);
        auto centerBtn = new QRadioButton(tr("Center"), mainWidget);
        auto rightBtn = new QRadioButton(tr("Right"), mainWidget);

        auto alignLayout = new QHBoxLayout();
        alignLayout->addWidget(noneBtn);
        alignLayout->addWidget(leftBtn);
        alignLayout->addWidget(centerBtn);
        alignLayout->addWidget(rightBtn);
        alignLayout->addStretch();

        mainLayout->addWidget(new QLabel(tr("Alignment:")), 1, 0, 1, 1);
        mainLayout->addLayout(alignLayout, 1, 1, 1, 3);

        auto buttonGroup = new QButtonGroup(mainWidget);
        buttonGroup->addButton(noneBtn, static_cast<int>(Alignment::None));
        buttonGroup->addButton(leftBtn, static_cast<int>(Alignment::Left));
        buttonGroup->addButton(centerBtn, static_cast<int>(Alignment::Center));
        buttonGroup->addButton(rightBtn, static_cast<int>(Alignment::Right));

        noneBtn->setChecked(true);
        connect(buttonGroup, static_cast<void(QButtonGroup::*)(int, bool)>(&QButtonGroup::buttonToggled),
                this, [this](int p_id, bool p_checked){
                    if (p_checked) {
                        m_alignment = static_cast<Alignment>(p_id);
                    }
                });
    }

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    setWindowTitle(p_title);
}

int TableInsertDialog::getRowCount() const
{
    return m_rowCountSpinBox->value();
}

int TableInsertDialog::getColumnCount() const
{
    return m_colCountSpinBox->value();
}

Alignment TableInsertDialog::getAlignment() const
{
    return m_alignment;
}
