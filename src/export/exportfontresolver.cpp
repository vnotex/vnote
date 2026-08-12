#include "exportfontresolver.h"

#include <QFontDatabase>
#include <QHash>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace vnotex {
namespace {
// Families known to ship as TrueType collections on Windows. backingFile() covers these too, but
// the list also guards the platforms where no file lookup is available.
const QStringList &ttcFamilyBlacklist() {
  static const QStringList c_families = {"Microsoft YaHei",
                                         "Microsoft YaHei UI",
                                         "Microsoft JhengHei",
                                         "Microsoft JhengHei UI",
                                         "SimSun",
                                         "NSimSun",
                                         "MS Gothic",
                                         "MS PGothic",
                                         "MS UI Gothic",
                                         "Yu Gothic",
                                         "MingLiU",
                                         "PMingLiU",
                                         "Cambria"};
  return c_families;
}

// Preferred text faces, best first. All ship as standalone files (never .ttc).
const QStringList &textCandidates() {
  static const QStringList c_candidates = {"Noto Sans CJK SC",
                                           "Noto Sans SC",
                                           "Source Han Sans SC",
                                           "Source Han Sans CN",
                                           "Source Han Sans",
                                           "PingFang SC",
                                           "Hiragino Sans GB",
                                           "Heiti SC",
                                           "WenQuanYi Micro Hei",
                                           "WenQuanYi Zen Hei",
                                           "Droid Sans Fallback",
                                           "SimHei",
                                           "KaiTi",
                                           "FangSong"};
  return c_candidates;
}

const QStringList &monoCandidates() {
  static const QStringList c_candidates = {"Noto Sans Mono CJK SC",    "Sarasa Mono SC",
                                           "Source Han Mono SC",       "Source Han Mono",
                                           "WenQuanYi Micro Hei Mono", "Sarasa Term SC"};
  return c_candidates;
}

bool isAscii(const QString &p_text) {
  for (const auto ch : p_text) {
    if (ch.unicode() > 0x7e) {
      return false;
    }
  }
  return true;
}

// Qt may report a family as "Family [foundry]".
bool familyMatches(const QString &p_family, const QString &p_candidate) {
  return p_family.compare(p_candidate, Qt::CaseInsensitive) == 0 ||
         p_family.startsWith(p_candidate + QStringLiteral(" ["), Qt::CaseInsensitive);
}
} // namespace

QStringList SystemFontProbe::families() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QFontDatabase::families();
#else
  return QFontDatabase().families();
#endif
}

bool SystemFontProbe::supportsSimplifiedChinese(const QString &p_family) const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const auto systems = QFontDatabase::writingSystems(p_family);
#else
  const auto systems = QFontDatabase().writingSystems(p_family);
#endif
  return systems.contains(QFontDatabase::SimplifiedChinese);
}

QString SystemFontProbe::backingFile(const QString &p_family) const {
#if defined(Q_OS_WIN)
  // family (lowercased) -> backing file, built once from the font registry.
  static const QHash<QString, QString> c_familyFiles = []() {
    QHash<QString, QString> files;
    const QStringList c_roots = {
        QStringLiteral(
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"),
        QStringLiteral(
            "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts")};
    for (const auto &root : c_roots) {
      QSettings reg(root, QSettings::NativeFormat);
      const auto keys = reg.childKeys();
      for (const auto &key : keys) {
        // "Noto Sans SC (TrueType)", "Microsoft YaHei & Microsoft YaHei UI (TrueType)", ...
        QString names = key;
        const int paren = names.lastIndexOf(QLatin1Char('('));
        if (paren > 0) {
          names = names.left(paren);
        }
        const auto file = reg.value(key).toString();
        const auto parts = names.split(QLatin1Char('&'), Qt::SkipEmptyParts);
        for (const auto &part : parts) {
          files.insert(part.trimmed().toLower(), file);
        }
      }
    }
    return files;
  }();

  return c_familyFiles.value(p_family.toLower());
#else
  // fontconfig-based wkhtmltopdf builds handle collections; no lookup needed.
  Q_UNUSED(p_family);
  return QString();
#endif
}

bool isLoadableByWkhtmltopdf(const IFontProbe &p_probe, const QString &p_family) {
  for (const auto &family : ttcFamilyBlacklist()) {
    if (p_family.compare(family, Qt::CaseInsensitive) == 0) {
      return false;
    }
  }

  return !p_probe.backingFile(p_family).endsWith(QStringLiteral(".ttc"), Qt::CaseInsensitive);
}

QByteArray resolveWkhtmltopdfFamily(const IFontProbe &p_probe, const QStringList &p_candidates,
                                    bool p_allowAnyUsableFallback) {
  const auto families = p_probe.families();

  for (const auto &candidate : p_candidates) {
    for (const auto &family : families) {
      if (!familyMatches(family, candidate) || !isLoadableByWkhtmltopdf(p_probe, family) ||
          !p_probe.supportsSimplifiedChinese(family)) {
        continue;
      }
      return candidate.toLatin1();
    }
  }

  if (!p_allowAnyUsableFallback) {
    return QByteArray();
  }

  for (const auto &family : families) {
    // '@'-prefixed families are the vertical-writing aliases Windows exposes; a family named in
    // CJK cannot survive the byte-level HTML patch.
    if (family.startsWith(QLatin1Char('@')) || !isAscii(family) ||
        !isLoadableByWkhtmltopdf(p_probe, family) || !p_probe.supportsSimplifiedChinese(family)) {
      continue;
    }
    return family.toLatin1();
  }

  return QByteArray();
}

ExportFontFamilies resolveWkhtmltopdfFonts(const IFontProbe &p_probe) {
  ExportFontFamilies fonts;
  fonts.m_text = resolveWkhtmltopdfFamily(p_probe, textCandidates());
  // No generic fallback for the monospace slot: an arbitrary CJK family is proportional, so it
  // would silently destroy code-block alignment. Fall back to the text family instead - it at
  // least keeps CJK inside code blocks legible, which a Latin-only monospace face would not.
  fonts.m_mono = resolveWkhtmltopdfFamily(p_probe, monoCandidates(), false);
  if (fonts.m_mono.isEmpty()) {
    fonts.m_mono = fonts.m_text;
  }
  return fonts;
}
} // namespace vnotex
