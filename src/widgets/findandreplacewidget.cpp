#include "findandreplacewidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QAction>
#include <QCheckBox>
#include <QKeyEvent>
#include <QTimer>

#include "lineedit.h"
#include "widgetsfactory.h"
#include <utils/iconutils.h>
#include <core/thememgr.h>
#include <core/vnotex.h>
#include "propertydefs.h"
#include "configmgr.h"
#include "widgetconfig.h"

using namespace vnotex;

FindAndReplaceWidget::FindAndReplaceWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_findTextTimer = new QTimer(this);
    m_findTextTimer->setSingleShot(true);
    m_findTextTimer->setInterval(500);
    connect(m_findTextTimer, &QTimer::timeout,
            this, [this]() {
                emit findTextChanged(getFindText(), getOptions());
            });

    setupUI();

    auto options = ConfigMgr::getInst().getWidgetConfig().getFindAndReplaceOptions();
    setFindOptions(options);
}

void FindAndReplaceWidget::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    // Title.
    {
        auto titleLayout = new QHBoxLayout();
        titleLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addLayout(titleLayout);

        auto label = new QLabel(tr("Find And Replace"), this);
        titleLayout->addWidget(label);

        const auto &themeMgr = VNoteX::getInst().getThemeMgr();
        auto iconFile = themeMgr.getIconFile(QStringLiteral("close.svg"));
        auto closeBtn = new QToolButton(this);
        closeBtn->setProperty(PropertyDefs::s_actionToolButton, true);
        titleLayout->addWidget(closeBtn);

        auto closeAct = new QAction(IconUtils::fetchIcon(iconFile), QString(), closeBtn);
        closeBtn->setDefaultAction(closeAct);
        connect(closeAct, &QAction::triggered,
                this, &FindAndReplaceWidget::close);
    }

    auto gridLayout = new QGridLayout();
    gridLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(gridLayout);

    // Find.
    {
        auto label = new QLabel(tr("Find:"), this);

        m_findLineEdit = WidgetsFactory::createLineEdit(this);
        m_findLineEdit->setPlaceholderText(tr("Search"));
        connect(m_findLineEdit, &QLineEdit::textChanged,
                m_findTextTimer, QOverload<>::of(&QTimer::start));

        auto findNextBtn = new QPushButton(tr("Find &Next"), this);
        findNextBtn->setDefault(true);
        connect(findNextBtn, &QPushButton::clicked,
                this, &FindAndReplaceWidget::findNext);

        auto findPrevBtn = new QPushButton(tr("Find &Previous"), this);
        connect(findPrevBtn, &QPushButton::clicked,
                this, &FindAndReplaceWidget::findPrevious);

        gridLayout->addWidget(label, 0, 0);
        gridLayout->addWidget(m_findLineEdit, 0, 1);
        gridLayout->addWidget(findNextBtn, 0, 2);
        gridLayout->addWidget(findPrevBtn, 0, 3);
    }

    // Replace.
    {
        auto label = new QLabel(tr("Replace with:"), this);

        m_replaceLineEdit = WidgetsFactory::createLineEdit(this);
        m_replaceLineEdit->setPlaceholderText(tr("\\1, \\2 for back reference in regular expression"));
        m_replaceRelatedWidgets.push_back(m_replaceLineEdit);

        auto replaceBtn = new QPushButton(tr("Replace"), this);
        connect(replaceBtn, &QPushButton::clicked,
                this, &FindAndReplaceWidget::replace);
        m_replaceRelatedWidgets.push_back(replaceBtn);

        auto replaceFindBtn = new QPushButton(tr("Replace And Find"), this);
        connect(replaceFindBtn, &QPushButton::clicked,
                this, &FindAndReplaceWidget::replaceAndFind);
        m_replaceRelatedWidgets.push_back(replaceFindBtn);

        auto replaceAllBtn = new QPushButton(tr("Replace All"), this);
        connect(replaceAllBtn, &QPushButton::clicked,
                this, &FindAndReplaceWidget::replaceAll);
        m_replaceRelatedWidgets.push_back(replaceAllBtn);

        gridLayout->addWidget(label, 1, 0);
        gridLayout->addWidget(m_replaceLineEdit, 1, 1);
        gridLayout->addWidget(replaceBtn, 1, 2);
        gridLayout->addWidget(replaceFindBtn, 1, 3);
        gridLayout->addWidget(replaceAllBtn, 1, 4);
    }

    // Options.
    {
        auto optionLayout = new QHBoxLayout();
        optionLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->addLayout(optionLayout, 2, 1, 1, 4);

        m_caseSensitiveCheckBox = WidgetsFactory::createCheckBox(tr("&Case sensitive"), this);
        connect(m_caseSensitiveCheckBox, &QCheckBox::stateChanged,
                this, &FindAndReplaceWidget::updateFindOptions);
        optionLayout->addWidget(m_caseSensitiveCheckBox);

        m_wholeWordOnlyCheckBox = WidgetsFactory::createCheckBox(tr("&Whole word only"), this);
        connect(m_wholeWordOnlyCheckBox, &QCheckBox::stateChanged,
                this, &FindAndReplaceWidget::updateFindOptions);
        optionLayout->addWidget(m_wholeWordOnlyCheckBox);

        m_regularExpressionCheckBox = WidgetsFactory::createCheckBox(tr("Re&gular expression"), this);
        connect(m_regularExpressionCheckBox, &QCheckBox::stateChanged,
                this, &FindAndReplaceWidget::updateFindOptions);
        optionLayout->addWidget(m_regularExpressionCheckBox);

        m_incrementalSearchCheckBox = WidgetsFactory::createCheckBox(tr("&Incremental search"), this);
        connect(m_incrementalSearchCheckBox, &QCheckBox::stateChanged,
                this, &FindAndReplaceWidget::updateFindOptions);
        optionLayout->addWidget(m_incrementalSearchCheckBox);

        optionLayout->addStretch();
    }
}

void FindAndReplaceWidget::close()
{
    hide();
    emit closed();
}

void FindAndReplaceWidget::setReplaceEnabled(bool p_enabled)
{
    for (auto widget : m_replaceRelatedWidgets) {
        widget->setEnabled(p_enabled);
    }
}

void FindAndReplaceWidget::keyPressEvent(QKeyEvent *p_event)
{
    switch (p_event->key()) {
    case Qt::Key_Escape:
        close();
        return;

    case Qt::Key_Return:
    {
        const int modifiers = p_event->modifiers();
        if (modifiers != Qt::ShiftModifier && modifiers != Qt::NoModifier) {
            break;
        }

        if (!m_findLineEdit->hasFocus() && !m_replaceLineEdit->hasFocus()) {
            break;
        }

        if (modifiers == Qt::ShiftModifier) {
            findPrevious();
        } else {
            findNext();
        }
        return;
    }

    default:
        break;
    }
    QWidget::keyPressEvent(p_event);
}

void FindAndReplaceWidget::findNext()
{
    m_findTextTimer->stop();
    auto text = m_findLineEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit findNextRequested(text, m_options);
}

void FindAndReplaceWidget::findPrevious()
{
    m_findTextTimer->stop();
    auto text = m_findLineEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit findNextRequested(text, m_options | FindOption::FindBackward);
}

void FindAndReplaceWidget::updateFindOptions()
{
    if (m_optionCheckBoxMuted) {
        return;
    }

    FindOptions options = FindOption::None;

    if (m_caseSensitiveCheckBox->isChecked()) {
        options |= FindOption::CaseSensitive;
    }
    if (m_wholeWordOnlyCheckBox->isChecked()) {
        options |= FindOption::WholeWordOnly;
    }
    if (m_regularExpressionCheckBox->isChecked()) {
        options |= FindOption::RegularExpression;
    }
    if (m_incrementalSearchCheckBox->isChecked()) {
        options |= FindOption::IncrementalSearch;
    }

    if (options == m_options) {
        return;
    }
    m_options = options;
    ConfigMgr::getInst().getWidgetConfig().setFindAndReplaceOptions(m_options);
    m_findTextTimer->start();
}

void FindAndReplaceWidget::replace()
{
    m_findTextTimer->stop();
    auto text = m_findLineEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit replaceRequested(text, m_options, m_replaceLineEdit->text());
}

void FindAndReplaceWidget::replaceAndFind()
{
    m_findTextTimer->stop();
    auto text = m_findLineEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit replaceRequested(text, m_options, m_replaceLineEdit->text());
    emit findNextRequested(text, m_options);
}

void FindAndReplaceWidget::replaceAll()
{
    m_findTextTimer->stop();
    auto text = m_findLineEdit->text();
    if (text.isEmpty()) {
        return;
    }
    emit replaceAllRequested(text, m_options, m_replaceLineEdit->text());
}

void FindAndReplaceWidget::setFindOptions(FindOptions p_options)
{
    if (p_options == m_options) {
        return;
    }

    m_optionCheckBoxMuted = true;
    m_options = p_options & ~FindOption::FindBackward;
    m_caseSensitiveCheckBox->setChecked(m_options & FindOption::CaseSensitive);
    m_wholeWordOnlyCheckBox->setChecked(m_options & FindOption::WholeWordOnly);
    m_regularExpressionCheckBox->setChecked(m_options & FindOption::RegularExpression);
    m_incrementalSearchCheckBox->setChecked(m_options & FindOption::IncrementalSearch);
    m_optionCheckBoxMuted = false;
}

void FindAndReplaceWidget::open(const QString &p_text)
{
    show();

    if (!p_text.isEmpty()) {
        m_findLineEdit->setText(p_text);
    }

    m_findLineEdit->setFocus();
    m_findLineEdit->selectAll();

    emit opened();
}

QString FindAndReplaceWidget::getFindText() const
{
    return m_findLineEdit->text();
}

FindOptions FindAndReplaceWidget::getOptions() const
{
    return m_options;
}
