// Link-time stub of StickerFactory for test_dashboardcontroller_preview.
//
// DashboardController only ever asks the factory whether a type-id has a
// creator; it never creates a widget (that is the board's job). Linking the
// real stickerfactory.cpp would drag in every built-in sticker widget -- and
// with them QtWidgets, ThemeService and the model/view stack -- into a test
// that must stay GUILESS. Same approach as stubs_updatecontroller.cpp.
//
// registerBuiltInCreators() is deliberately a no-op: tests register their own
// creators (which are never invoked).

#include <gui/services/stickerfactory.h>

using namespace vnotex;

StickerFactory::StickerFactory(QObject *p_parent) : QObject(p_parent) {}

StickerFactory::~StickerFactory() = default;

void StickerFactory::registerBuiltInCreators() {}

void StickerFactory::registerCreator(const QString &p_typeId, CreatorFunc p_creator) {
  m_creators.insert(p_typeId.toLower(), std::move(p_creator));
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
  Q_UNUSED(p_typeId)
  Q_UNUSED(p_services)
  Q_UNUSED(p_settings)
  Q_UNUSED(p_parent)
  return nullptr;
}
