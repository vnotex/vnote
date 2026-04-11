#include "unitedentry.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSizePolicy>
#include <QTimer>
#include <QTreeWidget>
#include <functional>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <utils/widgetutils.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

#include "entrypopup.h"
#include "entrywidgetfactory.h"
#include "iunitedentry.h"
#include "unitedentryhelper.h"
#include "unitedentrymgr.h"

using namespace vnotex;

UnitedEntry::UnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_mgr(p_mgr) {
  Q_ASSERT(m_mgr);

  m_processTimer = new QTimer(this);
  m_processTimer->setSingleShot(true);
  m_processTimer->setInterval(300);
  connect(m_processTimer, &QTimer::timeout, this, &UnitedEntry::processInput);

  setupUI();

  connect(qApp, &QApplication::focusChanged, this, &UnitedEntry::handleFocusChanged);

  connect(m_mgr, &UnitedEntryMgr::entryFinished, this, &UnitedEntry::handleEntryFinished);
  connect(m_mgr, &UnitedEntryMgr::entryItemActivated, this, &UnitedEntry::handleEntryItemActivated);
}

UnitedEntry::~UnitedEntry() { delete m_popup; }

void UnitedEntry::setupUI() {
  auto mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  m_comboBox = new QComboBox(this);
  m_comboBox->setEditable(true);
  m_comboBox->setInsertPolicy(QComboBox::NoInsert);
  m_comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_comboBox->setMinimumWidth(300);
  m_comboBox->lineEdit()->setPlaceholderText(tr("Type to command"));
  m_comboBox->lineEdit()->setClearButtonEnabled(true);
  m_comboBox->lineEdit()->installEventFilter(this);
  connect(m_comboBox->lineEdit(), &QLineEdit::textChanged, m_processTimer,
          QOverload<>::of(&QTimer::start));
  setFocusProxy(m_comboBox);
  mainLayout->addWidget(m_comboBox);

  setupActions();

  m_popup = new EntryPopup();
  m_popup->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
  m_popup->installEventFilter(this);
  m_popup->hide();
}

void UnitedEntry::setupActions() {
  auto *themeService = m_services.get<ThemeService>();
  const auto fg = themeService->paletteColor("widgets#unitedentry#icon#fg");
  const auto busyFg = themeService->paletteColor("widgets#unitedentry#icon#busy#fg");

  const auto menuIcon = IconUtils::fetchIcon(themeService->getIconFile("menu.svg"), fg);
  m_menuIconAction = m_comboBox->lineEdit()->addAction(menuIcon, QLineEdit::TrailingPosition);
  m_menuIconAction->setText(tr("Options"));

  auto menu = WidgetsFactory::createMenu(this);
  m_menuIconAction->setMenu(menu);

  {
    auto *configMgr = m_services.get<ConfigMgr2>();
    auto expandAct = menu->addAction(tr("Expand All"), this, [this, configMgr](bool checked) {
      configMgr->getWidgetConfig().setUnitedEntryExpandAllEnabled(checked);
      m_mgr->setExpandAllEnabled(checked);
    });
    expandAct->setCheckable(true);
    expandAct->setChecked(configMgr->getWidgetConfig().getUnitedEntryExpandAllEnabled());
    m_mgr->setExpandAllEnabled(expandAct->isChecked());
  }

  connect(m_menuIconAction, &QAction::triggered, this, [this]() {
    const auto pos = QCursor::pos();
    auto menu = m_menuIconAction->menu();
    menu->exec(pos);
  });

  const auto busyIcon = IconUtils::fetchIcon(themeService->getIconFile("busy.svg"), busyFg);
  m_busyIconAction = m_comboBox->lineEdit()->addAction(busyIcon, QLineEdit::TrailingPosition);
  m_busyIconAction->setText(tr("Busy"));
  m_busyIconAction->setVisible(false);
}

QAction *UnitedEntry::getActivateAction() {
  auto *themeService = m_services.get<ThemeService>();
  const auto fg = themeService->paletteColor("widgets#unitedentry#icon#fg");
  const auto icon = IconUtils::fetchIcon(themeService->getIconFile("united_entry.svg"), fg);

  auto act = new QAction(icon, tr("United Entry"), this);
  connect(act, &QAction::triggered, this, &UnitedEntry::activate);

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto shortcut = configMgr->getCoreConfig().getShortcut(CoreConfig::Shortcut::UnitedEntry);
  WidgetUtils::addActionShortcut(act, shortcut, Qt::ApplicationShortcut);

  return act;
}

void UnitedEntry::activate() {
  if (m_activated) {
    return;
  }

  if (!m_mgr->isInitialized()) {
    return;
  }

  m_activated = true;
  m_previousFocusWidget = QApplication::focusWidget();

  m_processTimer->stop();
  processInput();

  m_comboBox->lineEdit()->selectAll();
  m_comboBox->lineEdit()->setFocus();
}

void UnitedEntry::ensureActivated(QWidget *p_previousFocus) {
  if (m_activated) {
    return;
  }

  if (!m_mgr->isInitialized()) {
    return;
  }

  m_activated = true;
  m_previousFocusWidget = p_previousFocus;
}

void UnitedEntry::deactivate() {
  if (!m_activated) {
    return;
  }

  m_activated = false;
  m_previousFocusWidget = nullptr;

  m_popup->hide();
  m_comboBox->lineEdit()->clearFocus();
}

bool UnitedEntry::handleLineEditKeyPress(QKeyEvent *p_event) {
  auto *lineEdit = m_comboBox->lineEdit();
  const int key = p_event->key();
  const int modifiers = p_event->modifiers();
  IUnitedEntry::Action act = IUnitedEntry::Action::NextItem;
  switch (key) {
  case Qt::Key_BracketLeft:
    if (!WidgetUtils::isViControlModifier(modifiers)) {
      break;
    }
    Q_FALLTHROUGH();
  case Qt::Key_Escape:
    exitUnitedEntry();
    return true;

  // Up/Down Ctrl+K/J to navigate to previous/next item.
  case Qt::Key_Up:
    act = IUnitedEntry::Action::PreviousItem;
    Q_FALLTHROUGH();
  case Qt::Key_Down:
    if (m_lastEntry) {
      m_lastEntry->handleAction(act);
    } else if (m_entryListWidget && m_entryListWidget->isVisible()) {
      IUnitedEntry::handleActionCommon(act, m_entryListWidget.data());
    }
    return true;

  case Qt::Key_K:
    act = IUnitedEntry::Action::PreviousItem;
    Q_FALLTHROUGH();
  case Qt::Key_J:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      if (m_lastEntry) {
        m_lastEntry->handleAction(act);
      } else if (m_entryListWidget && m_entryListWidget->isVisible()) {
        IUnitedEntry::handleActionCommon(act, m_entryListWidget.data());
      }
      return true;
    }
    break;

  case Qt::Key_Enter:
    Q_FALLTHROUGH();
  case Qt::Key_Return:
    if (m_lastEntry) {
      m_lastEntry->handleAction(IUnitedEntry::Action::Activate);
    }
    return true;

  case Qt::Key_E:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Eliminate input till the entry name.
      const auto text = lineEdit->text();
      const auto entry = UnitedEntryHelper::parseUserEntry(text);
      if (!entry.m_name.isEmpty()) {
        lineEdit->setText(entry.m_name + QLatin1Char(' '));
      }
      return true;
    }
    break;

  case Qt::Key_F:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Select the entry name.
      const auto text = lineEdit->text();
      const auto entry = UnitedEntryHelper::parseUserEntry(text);
      if (!entry.m_name.isEmpty()) {
        lineEdit->setSelection(0, entry.m_name.size());
      }
      return true;
    }
    break;

  case Qt::Key_D:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Stop the entry.
      if (m_lastEntry) {
        m_lastEntry->stop();
      }
      return true;
    }
    break;

  case Qt::Key_L:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Go up one level.
      if (m_lastEntry) {
        m_lastEntry->handleAction(IUnitedEntry::Action::LevelUp);
      }
      return true;
    }
    break;

  case Qt::Key_I:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Expand/Collapse the item.
      if (m_lastEntry) {
        m_lastEntry->handleAction(IUnitedEntry::Action::ExpandCollapse);
      }
      return true;
    }
    break;

  case Qt::Key_B:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Expand/Collapse all the items.
      if (m_lastEntry) {
        m_lastEntry->handleAction(IUnitedEntry::Action::ExpandCollapseAll);
      }
      return true;
    }
    break;

  default:
    break;
  }

  return false;
}

void UnitedEntry::clear() {
  m_comboBox->lineEdit()->clear();
  m_comboBox->lineEdit()->setFocus();
}

void UnitedEntry::processInput() {
  m_hasPending = false;

  if (!m_activated) {
    return;
  }

  if (m_lastEntry && m_lastEntry->isOngoing()) {
    m_hasPending = true;
    m_lastEntry->stop();
    return;
  }

  const auto text = m_comboBox->lineEdit()->text();
  const auto entry = UnitedEntryHelper::parseUserEntry(text);
  if (entry.m_name.isEmpty()) {
    filterEntryListWidgetEntries(entry.m_name);
    popupWidget(getEntryListWidget());
    m_lastEntry.clear();
    return;
  }

  // Filter the help widget if space after entry name is not entered yet.
  if (entry.m_name == text.trimmed() && !text.back().isSpace()) {
    if (filterEntryListWidgetEntries(entry.m_name)) {
      popupWidget(getEntryListWidget());
      m_lastEntry.clear();
      return;
    }
  } else {
    if (!m_lastEntry || m_lastEntry->name() != entry.m_name) {
      m_lastEntry = m_mgr->findEntry(entry.m_name);
    }

    if (m_lastEntry) {
      setBusy(true);
      m_lastEntry->process(entry.m_args,
                           std::bind(&UnitedEntry::popupWidget, this, std::placeholders::_1));
      return;
    }
  }

  // No entry found.
  popupWidget(getInfoWidget(tr("Unknown entry: %1").arg(entry.m_name)));
  m_lastEntry.clear();
}

void UnitedEntry::popupWidget(const QSharedPointer<QWidget> &p_widget) {
  m_popup->setWidget(p_widget);
  updatePopupGeometry();
  m_popup->show();
}

const QSharedPointer<QTreeWidget> &UnitedEntry::getEntryListWidget() {
  if (!m_entryListWidget) {
    m_entryListWidget = EntryWidgetFactory::createTreeWidget(2);
    m_entryListWidget->setHeaderHidden(false);
    m_entryListWidget->setHeaderLabels(QStringList() << tr("Entry") << tr("Description"));

    const auto entries = m_mgr->getEntries();
    for (const auto &entry : entries) {
      m_entryListWidget->addTopLevelItem(
          new QTreeWidgetItem({entry->name(), entry->description()}));
    }
  }

  return m_entryListWidget;
}

QSharedPointer<QLabel> UnitedEntry::getInfoWidget(const QString &p_info) {
  return EntryWidgetFactory::createLabel(p_info);
}

bool UnitedEntry::filterEntryListWidgetEntries(const QString &p_name) {
  const auto &entryListWidget = getEntryListWidget();

  if (p_name.isEmpty()) {
    for (int i = 0; i < entryListWidget->topLevelItemCount(); ++i) {
      entryListWidget->topLevelItem(i)->setHidden(false);
    }
    return true;
  }

  auto items = entryListWidget->findItems(p_name, Qt::MatchStartsWith);
  for (int i = 0; i < entryListWidget->topLevelItemCount(); ++i) {
    entryListWidget->topLevelItem(i)->setHidden(true);
  }
  for (const auto &item : items) {
    item->setHidden(false);
  }
  return !items.isEmpty();
}

void UnitedEntry::handleEntryFinished(IUnitedEntry *p_entry) {
  if (p_entry != m_lastEntry.data()) {
    return;
  }

  setBusy(false);

  if (m_hasPending) {
    m_processTimer->start();
  }
}

void UnitedEntry::handleEntryItemActivated(IUnitedEntry *p_entry, bool p_quit,
                                           bool p_restoreFocus) {
  if (p_entry != m_lastEntry.data()) {
    return;
  }

  if (p_quit) {
    if (p_restoreFocus) {
      exitUnitedEntry();
    } else {
      deactivate();
    }
  }
}

void UnitedEntry::setBusy(bool p_busy) { m_busyIconAction->setVisible(p_busy); }

bool UnitedEntry::eventFilter(QObject *p_watched, QEvent *p_event) {
  if (p_watched == m_popup) {
    if (p_event->type() == QEvent::KeyPress) {
      auto eve = static_cast<QKeyEvent *>(p_event);
      switch (eve->key()) {
      case Qt::Key_BracketLeft:
        if (!WidgetUtils::isViControlModifier(eve->modifiers())) {
          break;
        }
        Q_FALLTHROUGH();
      case Qt::Key_Escape:
        exitUnitedEntry();
        return true;

      default:
        break;
      }
    }
  } else if (p_watched == m_comboBox->lineEdit()) {
    if (p_event->type() == QEvent::KeyPress) {
      auto eve = static_cast<QKeyEvent *>(p_event);
      if (handleLineEditKeyPress(eve)) {
        return true;
      }
    }
  }

  return QWidget::eventFilter(p_watched, p_event);
}

void UnitedEntry::exitUnitedEntry() {
  if (m_previousFocusWidget) {
    m_previousFocusWidget->setFocus();
  } else {
    if (auto *w = window()) {
      w->setFocus();
    }
  }
  deactivate();
}

QSize UnitedEntry::calculatePopupSize() const {
  const int minWidth = 400;
  const int maxWidth = 900;
  const int minHeight = 200;
  const int maxHeight = 600;

  int w = qMax(minWidth, static_cast<int>(window()->width() * 0.8));
  w = qMin(w, maxWidth);
  int h = qMax(minHeight, qMin(maxHeight, 400));
  return QSize(w, h);
}

void UnitedEntry::updatePopupGeometry() {
  QSize popupSize = calculatePopupSize();

  // Center the popup horizontally under the combo box.
  int comboCenter = m_comboBox->mapToGlobal(QPoint(m_comboBox->width() / 2, 0)).x();
  int x = comboCenter - popupSize.width() / 2;

  // Anchor vertically below the combo box.
  int y = m_comboBox->mapToGlobal(QPoint(0, m_comboBox->height())).y();

  m_popup->setGeometry(x, y, popupSize.width(), popupSize.height());
}

void UnitedEntry::handleFocusChanged(QWidget *p_old, QWidget *p_now) {
  if (!m_activated) {
    if (p_now == m_comboBox || p_now == m_comboBox->lineEdit()) {
      ensureActivated(p_old);
      m_processTimer->stop();
      processInput();
      return;
    }
  }

  if (m_activated && (!p_now || (p_now != m_comboBox && p_now != m_comboBox->lineEdit() &&
                                 !WidgetUtils::isOrAncestorOf(m_popup, p_now)))) {
    deactivate();
  }
}
