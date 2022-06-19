#include "unitedentry.h"

#include <QVBoxLayout>
#include <QSizePolicy>
#include <QAction>
#include <QFocusEvent>
#include <QDebug>
#include <QKeySequence>
#include <QShortcut>
#include <QApplication>
#include <QTimer>
#include <QTreeWidget>
#include <QLabel>
#include <QFont>
#include <QMenu>
#include <QMainWindow>

#include <widgets/lineeditwithsnippet.h>
#include <widgets/widgetsfactory.h>
#include <widgets/propertydefs.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <core/thememgr.h>
#include <core/vnotex.h>
#include <utils/widgetutils.h>
#include <utils/iconutils.h>

#include "entrypopup.h"
#include "entrywidgetfactory.h"
#include "unitedentrymgr.h"
#include "iunitedentry.h"
#include "unitedentryhelper.h"

using namespace vnotex;

UnitedEntry::UnitedEntry(QMainWindow *p_mainWindow)
    : QFrame(p_mainWindow),
      m_mainWindow(p_mainWindow)
{
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    m_processTimer->setInterval(300);
    connect(m_processTimer, &QTimer::timeout,
            this, &UnitedEntry::processInput);

    setupUI();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
    setWindowModality(Qt::ApplicationModal);
#else
    setWindowFlags(Qt::ToolTip);
#endif

    connect(qApp, &QApplication::focusChanged,
            this, &UnitedEntry::handleFocusChanged);

    connect(&UnitedEntryMgr::getInst(), &UnitedEntryMgr::entryFinished,
            this, &UnitedEntry::handleEntryFinished);
    connect(&UnitedEntryMgr::getInst(), &UnitedEntryMgr::entryItemActivated,
            this, &UnitedEntry::handleEntryItemActivated);
}

UnitedEntry::~UnitedEntry()
{
}

void UnitedEntry::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    // Line edit.
    m_lineEdit = WidgetsFactory::createLineEditWithSnippet(this);
    mainLayout->addWidget(m_lineEdit);
    m_lineEdit->setPlaceholderText(tr("Type to command"));
    m_lineEdit->setClearButtonEnabled(true);
    m_lineEdit->installEventFilter(this);
    connect(m_lineEdit, &QLineEdit::textChanged,
            m_processTimer, QOverload<>::of(&QTimer::start));
    setFocusProxy(m_lineEdit);

    setupActions();

    // Popup.
    m_popup = new EntryPopup(this);
    mainLayout->addWidget(m_popup);
    m_popup->installEventFilter(this);

    hide();
}

QAction *UnitedEntry::getTriggerAction()
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor("widgets#unitedentry#icon#fg");

    const auto icon = IconUtils::fetchIcon(themeMgr.getIconFile("united_entry.svg"), fg);
    auto act = new QAction(icon, tr("United Entry"), this);
    connect(act, &QAction::triggered,
            this, &UnitedEntry::activateUnitedEntry);

    const auto shortcut = ConfigMgr::getInst().getCoreConfig().getShortcut(CoreConfig::Shortcut::UnitedEntry);
    WidgetUtils::addActionShortcut(act, shortcut, Qt::ApplicationShortcut);

    return act;
}

void UnitedEntry::setupActions()
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor("widgets#unitedentry#icon#fg");
    const auto busyFg = themeMgr.paletteColor("widgets#unitedentry#icon#busy#fg");

    // Menu.
    const auto menuIcon = IconUtils::fetchIcon(themeMgr.getIconFile("menu.svg"), fg);
    m_menuIconAction = m_lineEdit->addAction(menuIcon, QLineEdit::ActionPosition::TrailingPosition);
    m_menuIconAction->setText(tr("Options"));

    auto menu = WidgetsFactory::createMenu(this);
    m_menuIconAction->setMenu(menu);

    {
        auto expandAct = menu->addAction(tr("Expand All"),
                                         this,
                                         [](bool checked) {
                                            ConfigMgr::getInst().getWidgetConfig().setUnitedEntryExpandAllEnabled(checked);
                                            UnitedEntryMgr::getInst().setExpandAllEnabled(checked);
                                         });
        expandAct->setCheckable(true);
        expandAct->setChecked(ConfigMgr::getInst().getWidgetConfig().getUnitedEntryExpandAllEnabled());
        UnitedEntryMgr::getInst().setExpandAllEnabled(expandAct->isChecked());
    }

    connect(m_menuIconAction, &QAction::triggered,
            this, [this]() {
                const auto pos = QCursor::pos();
                auto menu = m_menuIconAction->menu();
                menu->exec(pos);
            });

    // Busy.
    const auto busyIcon = IconUtils::fetchIcon(themeMgr.getIconFile("busy.svg"), busyFg);
    m_busyIconAction = m_lineEdit->addAction(busyIcon, QLineEdit::ActionPosition::TrailingPosition);
    m_busyIconAction->setText(tr("Busy"));
    m_busyIconAction->setVisible(false);
}

void UnitedEntry::activateUnitedEntry()
{
    if (m_activated) {
        return;
    }

    if (!UnitedEntryMgr::getInst().isInitialized()) {
        return;
    }

    m_activated = true;

    m_previousFocusWidget = QApplication::focusWidget();

    show();

    m_processTimer->stop();
    processInput();

    m_lineEdit->selectAll();
    m_lineEdit->setFocus();
}

void UnitedEntry::deactivateUnitedEntry()
{
    if (!m_activated) {
        return;
    }

    m_activated = false;
    m_previousFocusWidget = nullptr;

    hide();
}

bool UnitedEntry::handleLineEditKeyPress(QKeyEvent *p_event)
{
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
            const auto text = m_lineEdit->evaluatedText();
            const auto entry = UnitedEntryHelper::parseUserEntry(text);
            if (!entry.m_name.isEmpty()) {
                m_lineEdit->setText(entry.m_name + QLatin1Char(' '));
            }
            return true;
        }
        break;

    case Qt::Key_F:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Select the entry name.
            const auto text = m_lineEdit->evaluatedText();
            const auto entry = UnitedEntryHelper::parseUserEntry(text);
            if (!entry.m_name.isEmpty()) {
                m_lineEdit->setSelection(0, entry.m_name.size());
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

void UnitedEntry::clear()
{
    m_lineEdit->clear();
    m_lineEdit->setFocus();
}

void UnitedEntry::processInput()
{
    m_hasPending = false;

    if (!m_activated) {
        return;
    }

    if (m_lastEntry && m_lastEntry->isOngoing()) {
        m_hasPending = true;
        m_lastEntry->stop();
        return;
    }

    const auto text = m_lineEdit->evaluatedText();
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
        if (!m_lastEntry || m_lastEntry->name() == entry.m_name) {
            m_lastEntry = UnitedEntryMgr::getInst().findEntry(entry.m_name);
        }

        if (m_lastEntry) {
            // Found.
            setBusy(true);
            m_lastEntry->process(entry.m_args, std::bind(&UnitedEntry::popupWidget, this, std::placeholders::_1));
            return;
        }
    }

    // No entry found.
    popupWidget(getInfoWidget(tr("Unknown entry: %1").arg(entry.m_name)));
    m_lastEntry.clear();
}

void UnitedEntry::popupWidget(const QSharedPointer<QWidget> &p_widget)
{
    m_popup->setWidget(p_widget);

    m_lineEdit->setFocus();
}

const QSharedPointer<QTreeWidget> &UnitedEntry::getEntryListWidget()
{
    if (!m_entryListWidget) {
        m_entryListWidget = EntryWidgetFactory::createTreeWidget(2);
        m_entryListWidget->setHeaderHidden(false);
        m_entryListWidget->setHeaderLabels(QStringList() << tr("Entry") << tr("Description"));

        const auto entries = UnitedEntryMgr::getInst().getEntries();
        for (const auto &entry : entries) {
            m_entryListWidget->addTopLevelItem(new QTreeWidgetItem({entry->name(), entry->description()}));
        }
    }

    return m_entryListWidget;
}

QSharedPointer<QLabel> UnitedEntry::getInfoWidget(const QString &p_info)
{
    return EntryWidgetFactory::createLabel(p_info);
}

bool UnitedEntry::filterEntryListWidgetEntries(const QString &p_name)
{
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

void UnitedEntry::handleEntryFinished(IUnitedEntry *p_entry)
{
    if (p_entry != m_lastEntry.data()) {
        return;
    }

    setBusy(false);

    if (m_hasPending) {
        m_processTimer->start();
    }
}

void UnitedEntry::handleEntryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus)
{
    if (p_entry != m_lastEntry.data()) {
        return;
    }

    if (p_quit) {
        if (p_restoreFocus) {
            exitUnitedEntry();
        } else {
            deactivateUnitedEntry();
        }
    }
}

void UnitedEntry::setBusy(bool p_busy)
{
    m_busyIconAction->setVisible(p_busy);
}

bool UnitedEntry::eventFilter(QObject *p_watched, QEvent *p_event)
{
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
    } else if (p_watched == m_lineEdit) {
        if (p_event->type() == QEvent::KeyPress) {
            auto eve = static_cast<QKeyEvent *>(p_event);
            if (handleLineEditKeyPress(eve)) {
                return true;
            }
        }
    }

    return QFrame::eventFilter(p_watched, p_event);
}

void UnitedEntry::exitUnitedEntry()
{
    if (m_previousFocusWidget) {
        // Deactivate and focus previous widget.
        m_previousFocusWidget->setFocus();
    } else {
        m_mainWindow->setFocus();
    }
    deactivateUnitedEntry();
}

void UnitedEntry::showEvent(QShowEvent *p_event)
{
    QFrame::showEvent(p_event);

    // Fix input method issue.
    activateWindow();

    m_lineEdit->setFocus();

    updateGeometryToContents();
}

void UnitedEntry::updateGeometryToContents()
{
    adjustSize();

    const auto winSize = m_mainWindow->size();
    const auto sz = preferredSize();
    auto pos = parentWidget()->mapToGlobal(QPoint((winSize.width() - sz.width()) / 2,
                                                  (winSize.height() - sz.height()) / 2));
    setGeometry(QRect(pos, preferredSize()));
}

QSize UnitedEntry::preferredSize() const
{
    const int minWidth = 400;
    const int minHeight = 300;

    const auto winSize = m_mainWindow->size();
    int w = minWidth;
    int h = sizeHint().height();
    w = qMax(w, qMin(winSize.width() / 2, 900));
    h = qMax(h, qMin(winSize.height() - 300, 800));
    return QSize(qMax(minWidth, w), qMax(h, minHeight));
}

void UnitedEntry::handleFocusChanged(QWidget *p_old, QWidget *p_now)
{
    Q_UNUSED(p_old);
    if (m_activated &&
        (!p_now || (p_now != this && !WidgetUtils::isOrAncestorOf(this, p_now)))) {
        deactivateUnitedEntry();
    }
}
