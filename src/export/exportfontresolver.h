#ifndef EXPORTFONTRESOLVER_H
#define EXPORTFONTRESOLVER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace vnotex {
// Which font families to force at the head of the stack in the intermediate HTML handed to
// wkhtmltopdf (workaround 5 in wkhtmltopdfhtmlpatch.h).
//
// wkhtmltopdf 0.12.6's QtWebKit does NOT fall back per glyph along the CSS font-family list: it
// renders everything with the FIRST INSTALLED family. VNote's themes start their stack with
// Latin-only families, so every CJK codepoint misses and is served by the Qt system fallback
// (MS UI Gothic on Windows), whose JIS coverage lacks most simplified-only forms -> tofu.
//
// Naming a family is therefore only safe when it is installed AND actually loadable by
// wkhtmltopdf AND covers Simplified Chinese; anything else either changes nothing or makes the
// output worse. Empty means "inject no override".
struct ExportFontFamilies {
  QByteArray m_text;

  QByteArray m_mono;

  bool isEmpty() const { return m_text.isEmpty() && m_mono.isEmpty(); }
};

// The machine's font inventory. An interface so the resolution POLICY below can be unit-tested
// against synthetic inventories (a .ttc-backed font under an unlisted family name, a JIS-only
// font, a machine with no CJK font at all) without touching the real font database.
class IFontProbe {
public:
  virtual ~IFontProbe() = default;

  // Installed family names, as the font database reports them (may carry a " [foundry]" suffix).
  virtual QStringList families() const = 0;

  // Strictly Simplified Chinese. A Japanese (JIS) font is exactly what produces the tofu this
  // workaround exists to remove, and a Traditional-only font is no better.
  virtual bool supportsSimplifiedChinese(const QString &p_family) const = 0;

  // Path (or bare file name) of the font file backing p_family; empty when it cannot be
  // determined. Used to reject TrueType *collections*, which wkhtmltopdf cannot load at all.
  virtual QString backingFile(const QString &p_family) const = 0;
};

// The real inventory: QFontDatabase, plus the Windows font registry for backingFile().
class SystemFontProbe : public IFontProbe {
public:
  QStringList families() const override;

  bool supportsSimplifiedChinese(const QString &p_family) const override;

  QString backingFile(const QString &p_family) const override;
};

// True when p_family can actually be loaded by wkhtmltopdf: not a known .ttc family, and not
// backed by a .ttc file according to p_probe.
bool isLoadableByWkhtmltopdf(const IFontProbe &p_probe, const QString &p_family);

// The first candidate that is installed, loadable and Simplified-Chinese-capable. With
// p_allowAnyUsableFallback, any installed family meeting the same bar is accepted when no
// candidate matches; pass false when the candidate list encodes more than coverage (the monospace
// list does: an arbitrary CJK family is not monospace). Empty when nothing qualifies.
// The returned name is always ASCII: the HTML patch is byte-level and cannot re-encode.
QByteArray resolveWkhtmltopdfFamily(const IFontProbe &p_probe, const QStringList &p_candidates,
                                    bool p_allowAnyUsableFallback = true);

// Resolve both families. The monospace one falls back to the text one rather than to a Latin-only
// monospace face, which would bring the tofu back inside code blocks.
ExportFontFamilies resolveWkhtmltopdfFonts(const IFontProbe &p_probe);
} // namespace vnotex

#endif // EXPORTFONTRESOLVER_H
