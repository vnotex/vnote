#ifndef LOCALEUTILS_H
#define LOCALEUTILS_H

#include <QLocale>
#include <QStringList>

namespace vnotex {
// Match each UI preference before trying the next. The caller owns the final fallback
// and decides whether a value is usable (for example, trimming blank tip text).
template <typename Lookup>
QString findLocalizedValue(const QStringList &p_uiLanguages, const Lookup &p_lookup) {
  for (auto locale : p_uiLanguages) {
    locale.replace(QLatin1Char('-'), QLatin1Char('_'));
    auto value = p_lookup(locale);
    if (!value.isEmpty()) {
      return value;
    }

    // Script tags such as zh-Hans-CN must also match existing zh_CN resources.
    const auto canonical = QLocale(locale).name();
    if (canonical != locale) {
      value = p_lookup(canonical);
      if (!value.isEmpty()) {
        return value;
      }
    }

    const auto language = locale.section(QLatin1Char('_'), 0, 0);
    if (language != locale && language != canonical) {
      value = p_lookup(language);
      if (!value.isEmpty()) {
        return value;
      }
    }
  }
  return {};
}
} // namespace vnotex

#endif // LOCALEUTILS_H
