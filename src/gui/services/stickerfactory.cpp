#include "stickerfactory.h"

#include <QDebug>

#include <widgets/dashboard/calendarsticker.h>
#include <widgets/dashboard/greetingsticker.h>
#include <widgets/dashboard/historysticker.h>

using namespace vnotex;

StickerFactory::StickerFactory(QObject *p_parent) : QObject(p_parent) {}

StickerFactory::~StickerFactory() {}

void StickerFactory::registerBuiltInCreators() {
  registerCreator(QStringLiteral("calendar"),
                  [](ServiceLocator &p_services, const QJsonObject &p_settings,
                     QWidget *p_parent) -> Sticker * {
                    auto *sticker = new CalendarSticker(p_services, p_parent);
                    sticker->loadSettings(p_settings);
                    return sticker;
                  });
  registerCreator(QStringLiteral("history"),
                  [](ServiceLocator &p_services, const QJsonObject &p_settings,
                     QWidget *p_parent) -> Sticker * {
                    auto *sticker = new HistorySticker(p_services, p_parent);
                    sticker->loadSettings(p_settings);
                    return sticker;
                  });
  registerCreator(QStringLiteral("greeting"),
                  [](ServiceLocator &p_services, const QJsonObject &p_settings,
                     QWidget *p_parent) -> Sticker * {
                    auto *sticker = new GreetingSticker(p_services, p_parent);
                    sticker->loadSettings(p_settings);
                    return sticker;
                  });
}

void StickerFactory::registerCreator(const QString &p_typeId, CreatorFunc p_creator) {
  m_creators[p_typeId.toLower()] = std::move(p_creator);
}

void StickerFactory::unregisterCreator(const QString &p_typeId) {
  m_creators.remove(p_typeId.toLower());
}

bool StickerFactory::hasCreator(const QString &p_typeId) const {
  return m_creators.contains(p_typeId.toLower());
}

QStringList StickerFactory::registeredTypes() const {
  QStringList types = m_creators.keys();
  types.sort();
  return types;
}

Sticker *StickerFactory::create(const QString &p_typeId, ServiceLocator &p_services,
                                const QJsonObject &p_settings, QWidget *p_parent) const {
  auto it = m_creators.find(p_typeId.toLower());
  if (it == m_creators.end()) {
    qWarning() << "StickerFactory: no creator for sticker type" << p_typeId;
    return nullptr;
  }
  return it.value()(p_services, p_settings, p_parent);
}
