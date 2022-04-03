#include "unitedentry.h"

#include <QHBoxLayout>
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

#include <widgets/lineeditwithsnippet.h>
#include <widgets/widgetsfactory.h>
#include <widgets/propertydefs.h>
#include <widgets/mainwindow.h>
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

UnitedEntry::UnitedEntry(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    m_processTimer->setInterval(300);
    connect(m_processTimer, &QTimer::timeout,
            this, &UnitedEntry::processInput);

    setupUI();

    connect(qApp, &QApplication::focusChanged,
            this, &UnitedEntry::handleFocusChanged);

    connect(&UnitedEntryMgr::getInst(), &UnitedEntryMgr::entryFinished,
            this, &UnitedEntry::handleEntryFinished);
}

UnitedEntry::~UnitedEntry()
{
}

void UnitedEntry::setupUI()
{
    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);

    // Shortcut.
    const auto shortcut = ConfigMgr::getInst().getCoreConfig().getShortcut(CoreConfig::Shortcut::UnitedEntry);
    const QKeySequence kseq(shortcut);
    const auto shortcutText = kseq.isEmpty() ? QString() : kseq.toString(QKeySequence::NativeText);
    if (!kseq.isEmpty()) {
        auto sc = WidgetUtils::createShortcut(shortcut, this, Qt::ShortcutContext::ApplicationShortcut);
        if (sc) {
            connect(sc, &QShortcut::activated,
                    this, [this]() {
                        if (m_lineEdit->hasFocus()) {
                            return;
                        }

                        bool popupVisible = m_popup->isVisible();
                        if (popupVisible) {
                            // Make m_lineEdit->setFocus() work.
                            m_popup->hide();
                        }
                        m_lineEdit->setFocus();
                        if (popupVisible) {
                            m_popup->show();
                        }
                    });
        }
    }
    setToolTip(shortcutText.isEmpty() ? tr("United Entry") : tr("United Entry (%1)").arg(shortcutText));

    // Line edit.
    m_lineEdit = WidgetsFactory::createLineEditWithSnippet(this);
    mainLayout->addWidget(m_lineEdit);
    m_lineEdit->setToolTip(QString());
    m_lineEdit->setPlaceholderText(shortcutText.isEmpty() ? tr("Type to command") : tr("Type to command (%1)").arg(shortcutText));
    m_lineEdit->setClearButtonEnabled(true);
    m_lineEdit->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
    connect(m_lineEdit, &QLineEdit::textChanged,
            m_processTimer, QOverload<>::of(&QTimer::start));
    setFocusProxy(m_lineEdit);

    // Popup.
    m_popup = new EntryPopup(this);
    m_popup->installEventFilter(this);
    m_popup->hide();

    setupIcons();
}

void UnitedEntry::setupIcons()
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor("widgets#unitedentry#icon#fg");
    // Use QIcon::Disabled as the busy state.
    const auto busyFg = themeMgr.paletteColor("widgets#unitedentry#icon#busy#fg");
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(busyFg, QIcon::Disabled));

    const auto icon = IconUtils::fetchIcon(themeMgr.getIconFile("united_entry.svg"), colors);
    m_iconAction = m_lineEdit->addAction(icon, QLineEdit::ActionPosition::LeadingPosition);
    m_iconAction->setText(tr("Options"));

    // Menu.
    auto menu = WidgetsFactory::createMenu(this);
    m_iconAction->setMenu(menu);

    {
        auto expandAct = menu->addAction(tr("Expand All"),
                                         this,
                                         [this](bool checked) {
                                            ConfigMgr::getInst().getWidgetConfig().setUnitedEntryExpandAllEnabled(checked);
                                            UnitedEntryMgr::getInst().setExpandAllEnabled(checked);
                                         });
        expandAct->setCheckable(true);
        expandAct->setChecked(ConfigMgr::getInst().getWidgetConfig().getUnitedEntryExpandAllEnabled());
        UnitedEntryMgr::getInst().setExpandAllEnabled(expandAct->isChecked());
    }

    connect(m_iconAction, &QAction::triggered,
            this, [this]() {
                auto pos = mapToGlobal(QPoint(0, height()));
                auto menu = m_iconAction->menu();
                menu->exec(pos);
            });
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

    setSizePolicy(QSizePolicy::Policy::MinimumExpanding, QSizePolicy::Policy::Fixed);

    m_lineEdit->selectAll();
    m_lineEdit->setFocus();

    m_processTimer->start();
}

void UnitedEntry::deactivateUnitedEntry()
{
    if (!m_activated) {
        return;
    }

    m_activated = false;
    m_previousFocusWidget = nullptr;

    setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);

    m_popup->hide();
}

void UnitedEntry::handleFocusChanged(QWidget *p_old, QWidget *p_now)
{
    if (p_now == m_lineEdit) {
        activateUnitedEntry();
        if (!m_previousFocusWidget && p_old != this && !WidgetUtils::isOrAncestorOf(this, p_old)) {
            m_previousFocusWidget = p_old;
        }
        return;
    }

    if (!m_activated) {
        return;
    }

    if (!p_now || (p_now != this && !WidgetUtils::isOrAncestorOf(this, p_now))) {
        deactivateUnitedEntry();
    }
}

void UnitedEntry::keyPressEvent(QKeyEvent *p_event)
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
        break;

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
        break;

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
        }
        break;

    case Qt::Key_Enter:
        Q_FALLTHROUGH();
    case Qt::Key_Return:
        if (m_lastEntry) {
            m_lastEntry->handleAction(IUnitedEntry::Action::Activate);
        }
        break;

    case Qt::Key_E:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Eliminate input till the entry name.
            const auto text = m_lineEdit->evaluatedText();
            const auto entry = UnitedEntryHelper::parseUserEntry(text);
            if (!entry.m_name.isEmpty()) {
                m_lineEdit->setText(entry.m_name + QLatin1Char(' '));
            }
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
        }
        break;

    case Qt::Key_D:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Stop the entry.
            if (m_lastEntry) {
                m_lastEntry->stop();
            }
        }
        break;

    case Qt::Key_L:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Go up one level.
            if (m_lastEntry) {
                m_lastEntry->handleAction(IUnitedEntry::Action::LevelUp);
            }
        }
        break;

    case Qt::Key_I:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Expand/Collapse the item.
            if (m_lastEntry) {
                m_lastEntry->handleAction(IUnitedEntry::Action::ExpandCollapse);
            }
        }
        break;

    case Qt::Key_B:
        if (WidgetUtils::isViControlModifier(modifiers)) {
            // Expand/Collapse all the items.
            if (m_lastEntry) {
                m_lastEntry->handleAction(IUnitedEntry::Action::ExpandCollapseAll);
            }
        }
        break;

    default:
        break;
    }

    QWidget::keyPressEvent(p_event);
}

void UnitedEntry::clear()
{
    m_lineEdit->setFocus();
    m_lineEdit->clear();
}

void UnitedEntry::processInput()
{
    m_hasPending = false;

    if (!m_activated) {
        return;
    }

    if (m_iconAction->menu()->isVisible()) {
        // Do not display the popup which will hide the menu.
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
    m_popup->show();
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

void UnitedEntry::resizeEvent(QResizeEvent *p_event)
{
    QWidget::resizeEvent(p_event);

    updatePopupGeometry();
}

void UnitedEntry::updatePopupGeometry()
{
    m_popup->updateGeometryToContents();
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

void UnitedEntry::setBusy(bool p_busy)
{
    m_iconAction->setEnabled(!p_busy);
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
                // Need to call deactivateUnitedEntry() again since focusChanged is not triggered.
                deactivateUnitedEntry();
                return true;

            default:
                break;
            }
        }
    }

    return QWidget::eventFilter(p_watched, p_event);
}

void UnitedEntry::exitUnitedEntry()
{
    if (m_previousFocusWidget) {
        // Deactivate and focus previous widget.
        m_previousFocusWidget->setFocus();
    } else {
        VNoteX::getInst().getMainWindow()->setFocus();
    }
}
