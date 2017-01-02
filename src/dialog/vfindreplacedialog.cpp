#include "vfindreplacedialog.h"
#include <QtWidgets>

VFindReplaceDialog::VFindReplaceDialog(QWidget *p_parent)
    : QWidget(p_parent), m_options(0), m_replaceAvailable(true)
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
    m_findEdit->setPlaceholderText(tr("Enter text to search"));
    findLabel->setBuddy(m_findEdit);
    m_findNextBtn = new QPushButton(tr("Find &Next"));
    m_findNextBtn->setProperty("FlatBtn", true);
    m_findNextBtn->setDefault(true);
    m_findPrevBtn = new QPushButton(tr("Find &Previous"));
    m_findPrevBtn->setProperty("FlatBtn", true);

    // Replace
    QLabel *replaceLabel = new QLabel(tr("&Replace with:"));
    m_replaceEdit = new QLineEdit();
    m_replaceEdit->setPlaceholderText(tr("Enter text to replace with"));
    replaceLabel->setBuddy(m_replaceEdit);
    m_replaceBtn = new QPushButton(tr("R&eplace"));
    m_replaceBtn->setProperty("FlatBtn", true);
    m_replaceFindBtn = new QPushButton(tr("Replace && Fin&d"));
    m_replaceFindBtn->setProperty("FlatBtn", true);
    m_replaceAllBtn = new QPushButton(tr("Replace A&ll"));
    m_replaceAllBtn->setProperty("FlatBtn", true);
    m_advancedBtn = new QPushButton(tr("&Advanced >>"));
    m_advancedBtn->setProperty("FlatBtn", true);
    m_advancedBtn->setCheckable(true);

    // Options
    m_caseSensitiveCheck = new QCheckBox(tr("&Case sensitive"), this);
    connect(m_caseSensitiveCheck, &QCheckBox::stateChanged,
            this, &VFindReplaceDialog::optionBoxToggled);
    m_wholeWordOnlyCheck = new QCheckBox(tr("&Whole word only"), this);
    connect(m_wholeWordOnlyCheck, &QCheckBox::stateChanged,
            this, &VFindReplaceDialog::optionBoxToggled);
    m_regularExpressionCheck = new QCheckBox(tr("Re&gular expression"), this);
    connect(m_regularExpressionCheck, &QCheckBox::stateChanged,
            this, &VFindReplaceDialog::optionBoxToggled);
    m_incrementalSearchCheck = new QCheckBox(tr("&Incremental search"), this);
    connect(m_incrementalSearchCheck, &QCheckBox::stateChanged,
            this, &VFindReplaceDialog::optionBoxToggled);

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
    gridLayout->addWidget(m_caseSensitiveCheck, 2, 1);
    gridLayout->addWidget(m_wholeWordOnlyCheck, 2, 2);
    gridLayout->addWidget(m_regularExpressionCheck, 3, 1);
    gridLayout->addWidget(m_incrementalSearchCheck, 3, 2);
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

    m_caseSensitiveCheck->hide();
    m_wholeWordOnlyCheck->hide();
    m_regularExpressionCheck->hide();
    m_incrementalSearchCheck->hide();

    // Signals
    connect(m_closeBtn, &QPushButton::clicked,
            this, &VFindReplaceDialog::closeDialog);
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &VFindReplaceDialog::handleFindTextChanged);
    connect(m_advancedBtn, &QPushButton::toggled,
            this, &VFindReplaceDialog::advancedBtnToggled);
    connect(m_findNextBtn, SIGNAL(clicked(bool)),
            this, SLOT(findNext()));
    connect(m_findPrevBtn, SIGNAL(clicked(bool)),
            this, SLOT(findPrevious()));
    connect(m_replaceBtn, SIGNAL(clicked(bool)),
            this, SLOT(replace()));
    connect(m_replaceFindBtn, SIGNAL(clicked(bool)),
            this, SLOT(replaceFind()));
    connect(m_replaceAllBtn, SIGNAL(clicked(bool)),
            this, SLOT(replaceAll()));
}

void VFindReplaceDialog::closeDialog()
{
    if (this->isVisible()) {
        hide();
        emit dialogClosed();
    }
}

void VFindReplaceDialog::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        event->accept();
        closeDialog();
        return;

    case Qt::Key_Return:
    {
        int modifiers = event->modifiers();
        bool shift = false;
        if (modifiers == Qt::ShiftModifier) {
            shift = true;
        } else if (modifiers != Qt::NoModifier) {
            break;
        }
        if (!m_findEdit->hasFocus() && !m_replaceEdit->hasFocus()) {
            break;
        }
        event->accept();
        if (shift) {
            findPrevious();
        } else {
            findNext();
        }
        return;
    }

    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void VFindReplaceDialog::openDialog(QString p_text)
{
    show();
    if (!p_text.isEmpty()) {
        m_findEdit->setText(p_text);
    }
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void VFindReplaceDialog::handleFindTextChanged(const QString &p_text)
{
    emit findTextChanged(p_text, m_options);
}

void VFindReplaceDialog::advancedBtnToggled(bool p_checked)
{
    if (p_checked) {
        m_advancedBtn->setText("B&asic <<");
    } else {
        m_advancedBtn->setText("&Advanced <<");
    }

    m_caseSensitiveCheck->setVisible(p_checked);
    m_wholeWordOnlyCheck->setVisible(p_checked);
    m_regularExpressionCheck->setVisible(p_checked);
    m_incrementalSearchCheck->setVisible(p_checked);
}

void VFindReplaceDialog::optionBoxToggled(int p_state)
{
    QObject *obj = sender();
    FindOption opt = FindOption::CaseSensitive;
    if (obj == m_caseSensitiveCheck) {
        opt = FindOption::CaseSensitive;
    } else if (obj == m_wholeWordOnlyCheck) {
        opt = FindOption::WholeWordOnly;
    } else if (obj == m_regularExpressionCheck) {
        opt = FindOption::RegularExpression;
    } else {
        opt = FindOption::IncrementalSearch;
    }

    if (p_state) {
        m_options |= opt;
    } else {
        m_options &= ~opt;
    }
    emit findOptionChanged(m_options);
}

void VFindReplaceDialog::setOption(FindOption p_opt, bool p_enabled)
{
    if (p_opt == FindOption::CaseSensitive) {
        m_caseSensitiveCheck->setChecked(p_enabled);
    } else if (p_opt == FindOption::WholeWordOnly) {
        m_wholeWordOnlyCheck->setChecked(p_enabled);
    } else if (p_opt == FindOption::RegularExpression) {
        m_regularExpressionCheck->setChecked(p_enabled);
    } else if (p_opt == FindOption::IncrementalSearch) {
        m_incrementalSearchCheck->setChecked(p_enabled);
    } else {
        Q_ASSERT(false);
    }
}

void VFindReplaceDialog::findNext()
{
    QString text = m_findEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit findNext(text, m_options, true);
}

void VFindReplaceDialog::findPrevious()
{
    QString text = m_findEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit findNext(text, m_options, false);
}

void VFindReplaceDialog::replace()
{
    QString text = m_findEdit->text();
    if (text.isEmpty() || !m_replaceAvailable) {
        return;
    }
    QString replaceText = m_replaceEdit->text();
    emit replace(text, m_options, replaceText, false);
}

void VFindReplaceDialog::replaceFind()
{
    QString text = m_findEdit->text();
    if (text.isEmpty() || !m_replaceAvailable) {
        return;
    }
    QString replaceText = m_replaceEdit->text();
    emit replace(text, m_options, replaceText, true);
}

void VFindReplaceDialog::replaceAll()
{
    QString text = m_findEdit->text();
    if (text.isEmpty() || !m_replaceAvailable) {
        return;
    }
    QString replaceText = m_replaceEdit->text();
    emit replaceAll(text, m_options, replaceText);
}

void VFindReplaceDialog::updateState(DocType p_docType, bool p_editMode)
{
    if (p_editMode || p_docType == DocType::Html) {
        m_wholeWordOnlyCheck->setEnabled(true);
        m_regularExpressionCheck->setEnabled(true);
    } else {
        m_wholeWordOnlyCheck->setEnabled(false);
        m_regularExpressionCheck->setEnabled(false);
    }
    m_replaceAvailable = p_editMode;
}
