#include "vfindreplacedialog.h"
#include <QtWidgets>

VFindReplaceDialog::VFindReplaceDialog(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();
}

void VFindReplaceDialog::setupUI()
{
    QLabel *titleLabel = new QLabel(tr("Find/Replace"));
    titleLabel->setProperty("TitleLabel", true);
    m_closeBtn = new QPushButton(QIcon(":/resources/icons/close.svg"), "");
    m_closeBtn->setProperty("TitleBtn", true);
    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(m_closeBtn);
    titleLayout->setStretch(0, 1);
    titleLayout->setStretch(1, 0);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);

    // Find
    QLabel *findLabel = new QLabel(tr("&Find:"));
    m_findEdit = new QLineEdit();
    findLabel->setBuddy(m_findEdit);
    m_findNextBtn = new QPushButton(tr("Find Next"));
    m_findNextBtn->setProperty("FlatBtn", true);
    m_findNextBtn->setDefault(true);
    m_findPrevBtn = new QPushButton(tr("Find Previous"));
    m_findPrevBtn->setProperty("FlatBtn", true);

    // Replace
    QLabel *replaceLabel = new QLabel(tr("&Replace with:"));
    m_replaceEdit = new QLineEdit();
    replaceLabel->setBuddy(m_replaceEdit);
    m_replaceBtn = new QPushButton(tr("Replace"));
    m_replaceBtn->setProperty("FlatBtn", true);
    m_replaceFindBtn = new QPushButton(tr("Replace && Find"));
    m_replaceFindBtn->setProperty("FlatBtn", true);
    m_replaceAllBtn = new QPushButton(tr("Replace All"));
    m_replaceAllBtn->setProperty("FlatBtn", true);
    m_advancedBtn = new QPushButton(tr("Advanced"));
    m_advancedBtn->setProperty("FlatBtn", true);

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addWidget(findLabel, 0, 0);
    gridLayout->addWidget(m_findEdit, 0, 1);
    gridLayout->addWidget(m_findNextBtn, 0, 2);
    gridLayout->addWidget(m_findPrevBtn, 0, 3);
    gridLayout->addWidget(replaceLabel, 1, 0);
    gridLayout->addWidget(m_replaceEdit, 1, 1);
    gridLayout->addWidget(m_replaceBtn, 1, 2);
    gridLayout->addWidget(m_replaceFindBtn, 1, 3);
    gridLayout->addWidget(m_replaceAllBtn, 1, 4);
    gridLayout->addWidget(m_advancedBtn, 1, 5);
    gridLayout->setColumnStretch(0, 0);
    gridLayout->setColumnStretch(1, 4);
    gridLayout->setColumnStretch(2, 1);
    gridLayout->setColumnStretch(3, 1);
    gridLayout->setColumnStretch(4, 1);
    gridLayout->setColumnStretch(5, 1);
    gridLayout->setColumnStretch(6, 3);
    QMargins margin = gridLayout->contentsMargins();
    margin.setLeft(3);
    gridLayout->setContentsMargins(margin);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(titleLayout);
    mainLayout->addLayout(gridLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);

    // Signals
    connect(m_closeBtn, &QPushButton::clicked,
            this, &VFindReplaceDialog::closeDialog);
}

void VFindReplaceDialog::closeDialog()
{
    if (this->isVisible()) {
        this->hide();
        emit dialogClosed();
    }
}

void VFindReplaceDialog::showEvent(QShowEvent *event)
{
    m_findEdit->selectAll();
    QWidget::showEvent(event);
}

void VFindReplaceDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        closeDialog();
        return;
    }
    QWidget::keyPressEvent(event);
}
