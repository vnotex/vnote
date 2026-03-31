#include "findandreplacewidget2.h"

#include <QAction>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <utils/widgetutils.h>
#include <core/widgetconfig.h>
#include "propertydefs.h"
#include "widgetsfactory.h"

using namespace vnotex;

FindAndReplaceWidget2::FindAndReplaceWidget2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  m_findTextTimer = new QTimer(this);
  m_findTextTimer->setSingleShot(true);
  m_findTextTimer->setInterval(500);
  connect(m_findTextTimer, &QTimer::timeout, this,
          [this]() { emit findTextChanged(getFindText(), getOptions()); });

  setupUI();

  auto *configMgr = m_services.get<ConfigMgr2>();
  auto options = configMgr->getWidgetConfig().getFindAndReplaceOptions();
  setFindOptions(options);
}

void FindAndReplaceWidget2::setupUI() {
  auto mainLayout = new QVBoxLayout(this);

  // Title.
  {
    auto titleLayout = new QHBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(titleLayout);

    auto label = new QLabel(tr("Find And Replace"), this);
    titleLayout->addWidget(label);

    auto *themeService = m_services.get<ThemeService>();
    auto iconFile = themeService->getIconFile(QStringLiteral("close.svg"));
    auto closeBtn = new QToolButton(this);
    closeBtn->setProperty(PropertyDefs::c_actionToolButton, true);
    titleLayout->addWidget(closeBtn);

    auto closeAct = new QAction(IconUtils::fetchIcon(iconFile), QString(), closeBtn);
    closeAct->setToolTip(tr("Close"));
    closeBtn->setDefaultAction(closeAct);
    connect(closeAct, &QAction::triggered, this, &FindAndReplaceWidget2::close);
  }

  auto gridLayout = new QGridLayout();
  gridLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addLayout(gridLayout);

  // Find.
  {
    auto label = new QLabel(tr("Find:"), this);

    m_findLineEdit = WidgetsFactory::createLineEdit(this);
    m_findLineEdit->setPlaceholderText(tr("Search"));
    m_findLineEdit->setClearButtonEnabled(true);
    connect(m_findLineEdit, &QLineEdit::textChanged, m_findTextTimer,
            QOverload<>::of(&QTimer::start));

    setFocusProxy(m_findLineEdit);

    auto *configMgr = m_services.get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();

    auto findNextBtn = new QPushButton(tr("Find &Next"), this);
    WidgetUtils::addButtonShortcutText(findNextBtn,
                                       editorConfig.getShortcut(EditorConfig::FindNext));
    findNextBtn->setDefault(true);
    connect(findNextBtn, &QPushButton::clicked, this, &FindAndReplaceWidget2::findNext);

    auto findPrevBtn = new QPushButton(tr("Find &Previous"), this);
    WidgetUtils::addButtonShortcutText(findPrevBtn,
                                       editorConfig.getShortcut(EditorConfig::FindPrevious));
    connect(findPrevBtn, &QPushButton::clicked, this, &FindAndReplaceWidget2::findPrevious);

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
    connect(replaceBtn, &QPushButton::clicked, this, &FindAndReplaceWidget2::replace);
    m_replaceRelatedWidgets.push_back(replaceBtn);

    auto replaceFindBtn = new QPushButton(tr("Replace And Find"), this);
    connect(replaceFindBtn, &QPushButton::clicked, this, &FindAndReplaceWidget2::replaceAndFind);
    m_replaceRelatedWidgets.push_back(replaceFindBtn);

    auto replaceAllBtn = new QPushButton(tr("Replace All"), this);
    connect(replaceAllBtn, &QPushButton::clicked, this, &FindAndReplaceWidget2::replaceAll);
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
    connect(m_caseSensitiveCheckBox, &QCheckBox::checkStateChanged, this,
            &FindAndReplaceWidget2::updateFindOptions);
    optionLayout->addWidget(m_caseSensitiveCheckBox);

    m_wholeWordOnlyCheckBox = WidgetsFactory::createCheckBox(tr("&Whole word only"), this);
    connect(m_wholeWordOnlyCheckBox, &QCheckBox::checkStateChanged, this,
            &FindAndReplaceWidget2::updateFindOptions);
    optionLayout->addWidget(m_wholeWordOnlyCheckBox);

    m_regularExpressionCheckBox = WidgetsFactory::createCheckBox(tr("Re&gular expression"), this);
    connect(m_regularExpressionCheckBox, &QCheckBox::checkStateChanged, this,
            &FindAndReplaceWidget2::updateFindOptions);
    optionLayout->addWidget(m_regularExpressionCheckBox);

    m_incrementalSearchCheckBox = WidgetsFactory::createCheckBox(tr("&Incremental search"), this);
    connect(m_incrementalSearchCheckBox, &QCheckBox::checkStateChanged, this,
            &FindAndReplaceWidget2::updateFindOptions);
    optionLayout->addWidget(m_incrementalSearchCheckBox);

    optionLayout->addStretch();
  }
}

void FindAndReplaceWidget2::close() {
  hide();
  emit closed();
}

void FindAndReplaceWidget2::setReplaceEnabled(bool p_enabled) {
  for (auto widget : m_replaceRelatedWidgets) {
    widget->setEnabled(p_enabled);
  }
}

void FindAndReplaceWidget2::keyPressEvent(QKeyEvent *p_event) {
  switch (p_event->key()) {
  case Qt::Key_Escape:
    close();
    return;

  case Qt::Key_Return: {
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

void FindAndReplaceWidget2::findNext() {
  m_findTextTimer->stop();
  auto text = m_findLineEdit->text();
  if (text.isEmpty()) {
    return;
  }
  emit findNextRequested(text, m_options);
}

void FindAndReplaceWidget2::findPrevious() {
  m_findTextTimer->stop();
  auto text = m_findLineEdit->text();
  if (text.isEmpty()) {
    return;
  }
  emit findNextRequested(text, m_options | FindOption::FindBackward);
}

void FindAndReplaceWidget2::updateFindOptions() {
  if (m_optionCheckBoxMuted) {
    return;
  }

  FindOptions options = FindOption::FindNone;

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
  auto *configMgr = m_services.get<ConfigMgr2>();
  configMgr->getWidgetConfig().setFindAndReplaceOptions(m_options);
  m_findTextTimer->start();
}

void FindAndReplaceWidget2::replace() {
  m_findTextTimer->stop();
  auto text = m_findLineEdit->text();
  if (text.isEmpty()) {
    return;
  }
  emit replaceRequested(text, m_options, m_replaceLineEdit->text());
}

void FindAndReplaceWidget2::replaceAndFind() {
  m_findTextTimer->stop();
  auto text = m_findLineEdit->text();
  if (text.isEmpty()) {
    return;
  }
  emit replaceRequested(text, m_options, m_replaceLineEdit->text());
  emit findNextRequested(text, m_options);
}

void FindAndReplaceWidget2::replaceAll() {
  m_findTextTimer->stop();
  auto text = m_findLineEdit->text();
  if (text.isEmpty()) {
    return;
  }
  emit replaceAllRequested(text, m_options, m_replaceLineEdit->text());
}

void FindAndReplaceWidget2::setFindOptions(FindOptions p_options) {
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

void FindAndReplaceWidget2::open(const QString &p_text) {
  show();

  if (!p_text.isEmpty()) {
    m_findLineEdit->setText(p_text);
  }

  m_findLineEdit->setFocus();
  m_findLineEdit->selectAll();

  emit opened();
}

QString FindAndReplaceWidget2::getFindText() const { return m_findLineEdit->text(); }

FindOptions FindAndReplaceWidget2::getOptions() const { return m_options; }

void FindAndReplaceWidget2::setOptionsEnabled(FindOptions p_options, bool p_enabled) {
  if (p_options & FindOption::CaseSensitive) {
    m_caseSensitiveCheckBox->setEnabled(p_enabled);
  }
  if (p_options & FindOption::WholeWordOnly) {
    m_wholeWordOnlyCheckBox->setEnabled(p_enabled);
  }
  if (p_options & FindOption::RegularExpression) {
    m_regularExpressionCheckBox->setEnabled(p_enabled);
  }
  if (p_options & FindOption::IncrementalSearch) {
    m_incrementalSearchCheckBox->setEnabled(p_enabled);
  }
}
