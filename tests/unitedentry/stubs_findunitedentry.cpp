#include <QLabel>
#include <QTreeWidget>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/services/notebookcoreservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <unitedentry/entrywidgetfactory.h>
#include <unitedentry/helpunitedentry.h>
#include <unitedentry/historyunitedentry.h>
#include <unitedentry/unitedentryalias.h>
#include <unitedentry/unitedentryhelper.h>
#include <utils/utils.h>
#include <widgets/treewidget.h>

using namespace vnotex;

QSharedPointer<QTreeWidget> EntryWidgetFactory::createTreeWidget(int p_columnCount) {
  auto tree = QSharedPointer<QTreeWidget>::create();
  tree->setColumnCount(p_columnCount);
  return tree;
}

QSharedPointer<QLabel> EntryWidgetFactory::createLabel(const QString &p_info) {
  return QSharedPointer<QLabel>::create(p_info);
}

HelpUnitedEntry::HelpUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                 QObject *p_parent)
    : IUnitedEntry(QStringLiteral("help"), QString(), p_mgr, p_parent), m_services(p_services) {}

QSharedPointer<QWidget> HelpUnitedEntry::currentPopupWidget() const { return {}; }

void HelpUnitedEntry::initOnFirstProcess() {}

void HelpUnitedEntry::processInternal(
    const QString &, const std::function<void(const QSharedPointer<QWidget> &)> &) {
  emit finished();
}

UnitedEntryAlias::UnitedEntryAlias(const QString &p_name, const QString &p_description,
                                   const QString &p_value, UnitedEntryMgr *p_mgr, QObject *p_parent)
    : IUnitedEntry(p_name, p_description, p_mgr, p_parent), m_value(p_value) {
  Q_UNUSED(m_value);
}

UnitedEntryAlias::UnitedEntryAlias(const QJsonObject &, UnitedEntryMgr *p_mgr, QObject *p_parent)
    : IUnitedEntry(QStringLiteral("alias"), QString(), p_mgr, p_parent) {}

QString UnitedEntryAlias::description() const { return IUnitedEntry::description(); }

QJsonObject UnitedEntryAlias::toJson() const { return QJsonObject(); }

void UnitedEntryAlias::initOnFirstProcess() {}

void UnitedEntryAlias::processInternal(
    const QString &, const std::function<void(const QSharedPointer<QWidget> &)> &) {
  emit finished();
}

bool UnitedEntryAlias::isOngoing() const { return false; }

void UnitedEntryAlias::setOngoing(bool) {}

void UnitedEntryAlias::handleAction(Action) {}

QSharedPointer<QWidget> UnitedEntryAlias::currentPopupWidget() const { return {}; }

bool UnitedEntryAlias::isAliasOf(const IUnitedEntry *) const { return false; }

UnitedEntryHelper::UnitedEntryHelper(ThemeService *) {}

UnitedEntryHelper::UserEntry UnitedEntryHelper::parseUserEntry(const QString &) { return {}; }

const QIcon &UnitedEntryHelper::itemIcon(ItemType) const {
  static QIcon s_icon;
  return s_icon;
}

void UnitedEntryHelper::refreshIcons() {}

UnitedEntryHelper::ItemType UnitedEntryHelper::locationTypeToItemType(LocationType) {
  return UnitedEntryHelper::Other;
}

QString ThemeService::getIconFile(const QString &p_icon) const { return p_icon; }

QString ThemeService::paletteColor(const QString &) const { return QString(); }

QIcon IconUtils::fetchIcon(const QString &p_iconFile, const QString &) { return QIcon(p_iconFile); }

CoreConfig &ConfigMgr2::getCoreConfig() {
  static CoreConfig *s = nullptr;
  Q_ASSERT_X(false, "stub", "ConfigMgr2::getCoreConfig() called in unit test");
  return *s;
}

const QJsonArray &CoreConfig::getUnitedEntryAlias() const {
  static QJsonArray s;
  return s;
}

QJsonArray NotebookCoreService::listNotebooks() const { return QJsonArray(); }

void Utils::sleepWait(int) {}

void TreeWidget::selectParentItem(QTreeWidget *) {}

bool TreeWidget::isExpanded(const QTreeWidget *) { return false; }

HistoryUnitedEntry::HistoryUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                       QObject *p_parent)
    : IUnitedEntry(QStringLiteral("history"), QString(), p_mgr, p_parent), m_services(p_services),
      m_helper(nullptr) {}

QSharedPointer<QWidget> HistoryUnitedEntry::currentPopupWidget() const { return {}; }

void HistoryUnitedEntry::initOnFirstProcess() {}

void HistoryUnitedEntry::processInternal(
    const QString &, const std::function<void(const QSharedPointer<QWidget> &)> &) {
  emit finished();
}
