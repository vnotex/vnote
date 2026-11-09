#include "unitedentry.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <functional>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <utils/widgetutils.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

#include "entrypopup.h"
#include "entrypopupgeometry.h"
#include "entrywidgetfactory.h"
#include "iunitedentry.h"
#include "unitedentryhelper.h"
#include "unitedentrymgr.h"

using namespace vnotex;

namespace {
constexpr int c_maxInputWidth = 480;
} // namespace

UnitedEntry::UnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr, const QSize &p_iconSize,
                         QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_mgr(p_mgr), m_iconSize(p_iconSize) {
  Q_ASSERT(m_mgr);

  m_processTimer = new QTimer(this);
  m_processTimer->setSingleShot(true);
  m_processTimer->setInterval(300);
  connect(m_processTimer, &QTimer::timeout, this, &UnitedEntry::processInput);

  setupUI();

  connect(qApp, &QApplication::focusChanged, this, &UnitedEntry::handleFocusChanged);

  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          &UnitedEntry::handleAppStateChanged);

  connect(m_mgr, &UnitedEntryMgr::entryFinished, this, &UnitedEntry::handleEntryFinished);
  connect(m_mgr, &UnitedEntryMgr::entryItemActivated, this, &UnitedEntry::handleEntryItemActivated);

  auto *themeService = m_services.get<ThemeService>();
  connect(themeService, &ThemeService::themeChanged, this, &UnitedEntry::refreshIcons);
}

UnitedEntry::~UnitedEntry() { delete m_popup; }

void UnitedEntry::setupUI() {
  auto mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  m_toggleButton = new QToolButton(this);
  m_toggleButton->setAutoRaise(true);
  m_toggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_toggleButton->setToolTip(tr("United Entry"));
  // Match the toolbar's icon size (passed in by the toolbar helper); the
  // toolbar's setIconSize only propagates to buttons it creates for actions,
  // not to this nested custom widget.
  if (m_iconSize.isValid()) {
    m_toggleButton->setIconSize(m_iconSize);
  }
  m_toggleButton->setDefaultAction(getActivateAction());

  m_comboBox = WidgetsFactory::createEditableComboBox(this);
  m_comboBox->setInsertPolicy(QComboBox::NoInsert);
  m_comboBox->setCompleter(nullptr);
  m_comboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  m_comboBox->setMinimumWidth(300);
  m_comboBox->setMaximumWidth(c_maxInputWidth);
  m_comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setMaximumWidth(c_maxInputWidth);
  m_comboBox->lineEdit()->setPlaceholderText(tr("Type to search files by name in all notebooks"));
  m_comboBox->lineEdit()->setClearButtonEnabled(true);
  m_comboBox->lineEdit()->installEventFilter(this);
  m_comboBox->installEventFilter(this);
  connect(m_comboBox->lineEdit(), &QLineEdit::textChanged, m_processTimer,
          QOverload<>::of(&QTimer::start));
  connect(m_comboBox, QOverload<int>::of(&QComboBox::activated), this, [this]() {
    m_processTimer->stop();
    processInput();
  });
  setFocusProxy(m_comboBox);

  mainLayout->addWidget(m_toggleButton, 0, Qt::AlignVCenter);
  mainLayout->addWidget(m_comboBox);

  // Reserve the combo box's height (plus a small margin, since a focused input
  // box can grow slightly) on the whole widget so the toolbar height stays
  // constant when toggling between the icon and the input box. Reserving it on
  // the container (not the button) keeps the icon vertically centered and
  // aligned with the other toolbar icons.
  setMinimumHeight(m_comboBox->sizeHint().height() + 2);

  // Collapsed by default: show only the button. A hidden combo box
  // contributes no width, so the widget shrinks to the button.
  m_comboBox->hide();

  setupActions();

  m_popup = new EntryPopup();
  m_popup->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                          Qt::WindowDoesNotAcceptFocus);
  m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
  m_popup->installEventFilter(this);
  m_popup->hide();
}

void UnitedEntry::setupActions() {
  auto *themeService = m_services.get<ThemeService>();
  const auto busyFg = themeService->paletteColor("widgets#unitedentry#icon#busy#fg");

  const auto busyIcon = IconUtils::fetchIcon(themeService->getIconFile("busy.svg"), busyFg);
  m_busyIconAction = m_comboBox->lineEdit()->addAction(busyIcon, QLineEdit::TrailingPosition);
  m_busyIconAction->setText(tr("Busy"));
  m_busyIconAction->setVisible(false);
}

QAction *UnitedEntry::getActivateAction() {
  if (m_activateAction) {
    return m_activateAction;
  }

  auto *themeService = m_services.get<ThemeService>();
  const auto fg = themeService->paletteColor("widgets#toolbar#icon#fg");
  const auto disabledFg = themeService->paletteColor("widgets#toolbar#icon#disabled#fg");
  const QVector<IconUtils::OverriddenColor> colors = {
      IconUtils::OverriddenColor(fg, QIcon::Normal),
      IconUtils::OverriddenColor(disabledFg, QIcon::Disabled)};
  const auto icon = IconUtils::fetchIcon(themeService->getIconFile("united_entry.svg"), colors);

  m_activateAction = new QAction(icon, tr("United Entry"), this);
  connect(m_activateAction, &QAction::triggered, this, &UnitedEntry::activate);

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto shortcut = configMgr->getCoreConfig().getShortcut(CoreConfig::Shortcut::UnitedEntry);
  WidgetUtils::addActionShortcut(m_activateAction, shortcut, Qt::ApplicationShortcut);

  return m_activateAction;
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

  m_toggleButton->hide();
  m_comboBox->show();

  populateHistoryItems();
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
  m_toggleButton->hide();
  m_comboBox->show();
  populateHistoryItems();
}

void UnitedEntry::deactivate() {
  if (!m_activated) {
    return;
  }

  m_activated = false;
  m_previousFocusWidget = nullptr;
  m_popupNeedsRestore = false;

  m_popup->hide();
  m_comboBox->lineEdit()->clearFocus();
  m_comboBox->hide();
  m_toggleButton->show();
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
    } else if (m_entryListWidget && m_entryListWidget->isVisible()) {
      handleEntryListItemActivated(m_entryListWidget->currentItem());
    }
    return true;

  case Qt::Key_E:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Keep only an explicit command key; filename queries have no key to retain.
      const auto text = lineEdit->text();
      const auto entry = UnitedEntryHelper::parseUserEntry(text);
      if (!entry.m_isCommand) {
        lineEdit->clear();
      } else if (!entry.m_name.isEmpty()) {
        lineEdit->setText(QLatin1Char('>') + entry.m_name + QLatin1Char(' '));
      }
      return true;
    }
    break;

  case Qt::Key_F:
    if (WidgetUtils::isViControlModifier(modifiers)) {
      // Select the explicit key without its marker, or the entire filename query.
      const auto text = lineEdit->text();
      const auto entry = UnitedEntryHelper::parseUserEntry(text);
      if (!entry.m_isCommand) {
        lineEdit->selectAll();
      } else if (!entry.m_name.isEmpty()) {
        auto start = text.indexOf(QLatin1Char('>')) + 1;
        while (text.at(start).isSpace()) {
          ++start;
        }
        lineEdit->setSelection(start, entry.m_name.size());
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

bool UnitedEntry::isUnitedEntryShortcut(QKeyEvent *p_event) const {
  const int key = p_event->key();
  const int modifiers = p_event->modifiers();
  switch (key) {
  case Qt::Key_Escape:
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Enter:
  case Qt::Key_Return:
    return true;

  case Qt::Key_BracketLeft:
  case Qt::Key_J:
  case Qt::Key_K:
  case Qt::Key_L:
  case Qt::Key_I:
  case Qt::Key_B:
  case Qt::Key_E:
  case Qt::Key_F:
  case Qt::Key_D:
    return WidgetUtils::isViControlModifier(modifiers);

  default:
    return false;
  }
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
    filterEntryListWidgetEntries(entry.m_name, entry.m_isCommand);
    popupWidget(getEntryListWidget());
    m_lastEntry.clear();
    return;
  }

  // Suggest explicit keys until a separating whitespace or arguments are entered.
  if (entry.m_isCommand && entry.m_args.isEmpty() && !text.back().isSpace()) {
    if (filterEntryListWidgetEntries(entry.m_name, true)) {
      popupWidget(getEntryListWidget());
      m_lastEntry.clear();
      return;
    }
  } else {
    if (!m_lastEntry || m_lastEntry->name() != entry.m_name) {
      m_lastEntry = m_mgr->resolveEntry(entry.m_name);
    }

    if (m_lastEntry) {
      m_mgr->setExpandAllEnabled(
          m_services.get<ConfigMgr2>()->getWidgetConfig().getUnitedEntryExpandAllEnabled());
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
  if (!m_popup->isVisible()) {
    m_popup->show();
  }
}

const QSharedPointer<QTreeWidget> &UnitedEntry::getEntryListWidget() {
  if (!m_entryListWidget) {
    m_entryListWidget = EntryWidgetFactory::createTreeWidget(2);
    m_entryListWidget->setHeaderHidden(false);
    m_entryListWidget->setHeaderLabels(QStringList() << tr("Entry") << tr("Description"));

    // The discovery row has no command key; activation enters command mode.
    m_entryListWidget->addTopLevelItem(new QTreeWidgetItem({tr("Show and run entries >")}));
    const auto entries = m_mgr->getEntries();
    for (const auto &entry : entries) {
      auto *item = new QTreeWidgetItem({entry->name(), entry->description()});
      item->setData(0, Qt::UserRole, entry->name());
      m_entryListWidget->addTopLevelItem(item);
    }

    connect(m_entryListWidget.data(), &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *p_item, int) { handleEntryListItemActivated(p_item); });
  }

  return m_entryListWidget;
}

QSharedPointer<QLabel> UnitedEntry::getInfoWidget(const QString &p_info) {
  return EntryWidgetFactory::createLabel(p_info);
}

bool UnitedEntry::filterEntryListWidgetEntries(const QString &p_name, bool p_isCommand) {
  const auto &entryListWidget = getEntryListWidget();
  entryListWidget->setHeaderHidden(!p_isCommand);
  entryListWidget->setColumnHidden(1, !p_isCommand);

  QTreeWidgetItem *firstVisible = nullptr;
  QTreeWidgetItem *exactMatch = nullptr;
  for (int i = 0; i < entryListWidget->topLevelItemCount(); ++i) {
    auto *item = entryListWidget->topLevelItem(i);
    const auto key = item->data(0, Qt::UserRole).toString();
    const bool visible =
        p_isCommand ? !key.isEmpty() && key.startsWith(p_name, Qt::CaseInsensitive) : key.isEmpty();
    item->setHidden(!visible);
    if (visible && !firstVisible) {
      firstVisible = item;
    }
    if (p_isCommand && visible && key == p_name) {
      exactMatch = item;
    }
  }
  auto *current = entryListWidget->currentItem();
  if (exactMatch) {
    // Keep case-distinct aliases consistent with the exact-case-first resolver.
    entryListWidget->setCurrentItem(exactMatch);
  } else if (!current || current->isHidden()) {
    entryListWidget->setCurrentItem(firstVisible);
  }
  return firstVisible != nullptr;
}

void UnitedEntry::handleEntryListItemActivated(QTreeWidgetItem *p_item) {
  if (!p_item) {
    return;
  }

  const auto name = p_item->data(0, Qt::UserRole).toString();
  auto *lineEdit = m_comboBox->lineEdit();
  lineEdit->setText(name.isEmpty() ? QStringLiteral(">")
                                   : QLatin1Char('>') + name + QLatin1Char(' '));
  lineEdit->setFocus();

  // setText's textChanged only debounce-starts the timer; call directly for
  // immediate resolution.
  m_processTimer->stop();
  processInput();
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
    // Save to history before deactivating.
    auto *configMgr = m_services.get<ConfigMgr2>();
    configMgr->getSessionConfig().addUnitedEntryHistory(m_comboBox->lineEdit()->text());

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
  } else if (p_watched == m_comboBox || p_watched == m_comboBox->lineEdit()) {
    if (p_event->type() == QEvent::ShortcutOverride) {
      auto eve = static_cast<QKeyEvent *>(p_event);
      if (isUnitedEntryShortcut(eve)) {
        eve->accept();
        return true;
      }
    } else if (p_event->type() == QEvent::KeyPress) {
      auto eve = static_cast<QKeyEvent *>(p_event);
      if (handleLineEditKeyPress(eve)) {
        return true;
      }
    } else if (p_event->type() == QEvent::MouseButtonPress) {
      // Returning by clicking the input keeps focus unchanged, so focusChanged
      // never fires; re-show the popup if we are still activated. Do NOT consume
      // the event, or the click won't position the text cursor.
      scheduleRestorePopup();
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

void UnitedEntry::updatePopupGeometry() {
  EntryPopupGeometry::Metrics metrics;
  metrics.m_anchorCenterX = m_comboBox->mapToGlobal(QPoint(m_comboBox->width() / 2, 0)).x();
  metrics.m_anchorBottomY = m_comboBox->mapToGlobal(QPoint(0, m_comboBox->height())).y();

  // The stylesheet is applied before any widget is constructed (main.cpp), but
  // polish the popup explicitly so its font is final before it is measured.
  // Measure the popup's own font (not this widget's): the popup and its tree
  // use the application font, whereas UnitedEntry carries the toolbar font.
  m_popup->ensurePolished();
  metrics.m_charWidth = m_popup->fontMetrics().averageCharWidth();

  // Clamp against the screen that actually contains the anchor, not merely the
  // one this widget's window is associated with: a main window straddling two
  // monitors can have the toolbar on the other one.
  const QPoint anchor(metrics.m_anchorCenterX, metrics.m_anchorBottomY);
  const QScreen *scr = QGuiApplication::screenAt(anchor);
  if (!scr) {
    scr = screen();
  }
  if (scr) {
    metrics.m_availableRect = scr->availableGeometry();
  }

  m_popup->setGeometry(EntryPopupGeometry::calculateGeometry(metrics));
}

void UnitedEntry::handleFocusChanged(QWidget *p_old, QWidget *p_now) {
  if (!m_activated) {
    if (p_now == m_comboBox || p_now == m_comboBox->lineEdit()) {
      ensureActivated(p_old);
      m_processTimer->stop();
      // Suppress processInput when native dropdown is showing with history items.
      if (m_comboBox->view()->isVisible() && m_comboBox->count() > 0) {
        return;
      }
      processInput();
      return;
    }
  }

  if (m_activated && p_now && p_now != m_comboBox && p_now != m_comboBox->lineEdit() &&
      !WidgetUtils::isOrAncestorOf(m_popup, p_now)) {
    if (m_deactivatePending) {
      return;
    }
    m_deactivatePending = true;
    QTimer::singleShot(0, this, [this]() {
      m_deactivatePending = false;
      if (!m_activated) {
        return;
      }
      auto *fw = QApplication::focusWidget();
      if (fw == m_comboBox || fw == m_comboBox->lineEdit() ||
          WidgetUtils::isOrAncestorOf(m_popup, fw)) {
        return;
      }
      deactivate();
    });
  }
}

void UnitedEntry::handleAppStateChanged(Qt::ApplicationState p_state) {
  if (p_state != Qt::ApplicationActive) {
    // The app is deactivating; Windows hides the parentless Qt::Tool popup at
    // the native level. Remember to re-map it when we become active again.
    if (m_activated) {
      m_popupNeedsRestore = true;
    }
    return;
  }
  if (!m_activated) {
    return;
  }
  scheduleRestorePopup();
}

void UnitedEntry::scheduleRestorePopup() {
  if (!m_activated || !m_popupNeedsRestore || m_restorePending) {
    return;
  }
  m_restorePending = true;
  // Defer so focus restoration / the activating click settle first.
  QTimer::singleShot(0, this, [this]() {
    m_restorePending = false;
    restorePopupVisibility();
  });
}

void UnitedEntry::restorePopupVisibility() {
  if (!m_activated) {
    return;
  }

  // Only restore while the input still holds focus; otherwise the normal
  // deactivate flow in handleFocusChanged should win. Return without clearing
  // m_popupNeedsRestore so a later trigger can retry once focus settles.
  auto *fw = QApplication::focusWidget();
  if (fw != m_comboBox && fw != m_comboBox->lineEdit()) {
    return;
  }

  // Suppress when the native combo dropdown is showing history items.
  if (m_comboBox->view()->isVisible() && m_comboBox->count() > 0) {
    return;
  }

  m_popupNeedsRestore = false;

  if (m_popup->hasWidget()) {
    // Re-show existing content WITHOUT reprocessing, so an ongoing async entry
    // (e.g. find) is not cancelled. Force a native re-map: Windows hides
    // Qt::Tool windows at the native level without clearing the widget's
    // visible state, so a plain show() can be a no-op.
    updatePopupGeometry();
    m_popup->hide();
    m_popup->show();
    m_popup->raise();
  } else {
    // No content yet (native dropdown suppressed the initial processInput); no
    // entry can be ongoing here, so building content is safe.
    m_processTimer->stop();
    processInput();
  }
}

void UnitedEntry::refreshIcons() {
  auto *themeService = m_services.get<ThemeService>();
  const auto busyFg = themeService->paletteColor("widgets#unitedentry#icon#busy#fg");

  m_busyIconAction->setIcon(IconUtils::fetchIcon(themeService->getIconFile("busy.svg"), busyFg));

  if (m_activateAction) {
    const auto toolbarFg = themeService->paletteColor("widgets#toolbar#icon#fg");
    const auto disabledFg = themeService->paletteColor("widgets#toolbar#icon#disabled#fg");
    const QVector<IconUtils::OverriddenColor> colors = {
        IconUtils::OverriddenColor(toolbarFg, QIcon::Normal),
        IconUtils::OverriddenColor(disabledFg, QIcon::Disabled)};
    m_activateAction->setIcon(
        IconUtils::fetchIcon(themeService->getIconFile("united_entry.svg"), colors));
  }
}

void UnitedEntry::populateHistoryItems() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &history = configMgr->getSessionConfig().getUnitedEntryHistory();
  {
    QSignalBlocker blocker(m_comboBox);
    auto currentText = m_comboBox->lineEdit()->text();
    m_comboBox->clear();
    m_comboBox->addItems(history);
    m_comboBox->lineEdit()->setText(currentText);
  }
}
