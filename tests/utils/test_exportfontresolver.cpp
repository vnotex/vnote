// test_exportfontresolver.cpp - Tests for the wkhtmltopdf CJK font resolution policy.
//
// Regression context: wkhtmltopdf 0.12.6's QtWebKit does not fall back per glyph along the CSS
// font-family list; it renders everything with the first INSTALLED family. VNote's themes name
// Latin-only families first, so CJK text was served by the Qt system fallback (MS UI Gothic,
// JIS coverage) and every simplified-only character came out as a blank square. The exporter
// therefore forces one family at the head of the stack - but only a family that is installed,
// actually loadable by wkhtmltopdf (never a .ttc collection) and covers Simplified Chinese.
// Naming anything else either changes nothing or makes the output worse.
#include <QtTest>

#include <export/exportfontresolver.h>

using namespace vnotex;

namespace tests {

// A synthetic font inventory, so the policy can be exercised against machines we do not have.
class FakeFontProbe : public IFontProbe {
public:
  struct Entry {
    QString m_family;
    bool m_simplifiedChinese = false;
    QString m_file;
  };

  explicit FakeFontProbe(const QVector<Entry> &p_entries) : m_entries(p_entries) {}

  QStringList families() const override {
    QStringList names;
    for (const auto &entry : m_entries) {
      names << entry.m_family;
    }
    return names;
  }

  bool supportsSimplifiedChinese(const QString &p_family) const override {
    return find(p_family).m_simplifiedChinese;
  }

  QString backingFile(const QString &p_family) const override { return find(p_family).m_file; }

private:
  Entry find(const QString &p_family) const {
    for (const auto &entry : m_entries) {
      if (entry.m_family.compare(p_family, Qt::CaseInsensitive) == 0) {
        return entry;
      }
    }
    return Entry();
  }

  QVector<FakeFontProbe::Entry> m_entries;
};

static FakeFontProbe::Entry font(const QString &p_family, bool p_sc, const QString &p_file) {
  FakeFontProbe::Entry entry;
  entry.m_family = p_family;
  entry.m_simplifiedChinese = p_sc;
  entry.m_file = p_file;
  return entry;
}

class TestExportFontResolver : public QObject {
  Q_OBJECT

private slots:
  void testPrefersTheFirstCandidate();
  void testSkipsCandidateWithoutSimplifiedChinese();
  void testSkipsTtcBackedCandidate();
  void testSkipsBlacklistedTtcFamilyWithoutFileInfo();
  void testFallsBackToAnyUsableFamily();
  void testFallbackSkipsJapaneseOnlyFont();
  void testFallbackSkipsNonAsciiFamilyName();
  void testEmptyWhenNoCjkFontIsInstalled();
  void testMonoFallsBackToTheTextFamily();
  void testMonoNeverTakesAProportionalFallback();
  void testMonoIsUsedWhenAvailable();
};

void TestExportFontResolver::testPrefersTheFirstCandidate() {
  FakeFontProbe probe({font("Segoe UI", false, "segoeui.ttf"),
                       font("Noto Sans SC", true, "NotoSansSC-VF.ttf"),
                       font("SimHei", true, "simhei.ttf")});
  const auto fonts = resolveWkhtmltopdfFonts(probe);
  QCOMPARE(fonts.m_text, QByteArray("Noto Sans SC"));
}

void TestExportFontResolver::testSkipsCandidateWithoutSimplifiedChinese() {
  // Installed under a candidate name but with no simplified coverage: naming it would keep the
  // tofu while silently suppressing the "no usable font" warning.
  FakeFontProbe probe(
      {font("Noto Sans SC", false, "NotoSansSC-VF.ttf"), font("SimHei", true, "simhei.ttf")});
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray("SimHei"));
}

void TestExportFontResolver::testSkipsTtcBackedCandidate() {
  // A collection is invisible to wkhtmltopdf even under a preferred family name.
  FakeFontProbe probe({font("Source Han Sans SC", true, "SourceHanSans.ttc"),
                       font("Noto Sans SC", true, "NotoSansSC-VF.ttf")});
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray("Noto Sans SC"));
}

void TestExportFontResolver::testSkipsBlacklistedTtcFamilyWithoutFileInfo() {
  // No file information available (non-Windows probe): the name blacklist still rejects the
  // known collections.
  FakeFontProbe probe({font("Microsoft YaHei", true, QString()), font("SimSun", true, QString())});
  QVERIFY(!isLoadableByWkhtmltopdf(probe, QStringLiteral("Microsoft YaHei")));
  QVERIFY(!isLoadableByWkhtmltopdf(probe, QStringLiteral("SimSun")));
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray());
}

void TestExportFontResolver::testFallsBackToAnyUsableFamily() {
  // No curated candidate is installed: take any installed, loadable, SC-capable family rather
  // than give up (a Chinese Linux box may only have a distro-specific font).
  FakeFontProbe probe({font("Segoe UI", false, "segoeui.ttf"),
                       font("Some Distro Han Sans", true, "distro-han.otf")});
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray("Some Distro Han Sans"));
}

void TestExportFontResolver::testFallbackSkipsJapaneseOnlyFont() {
  // MS UI Gothic is precisely the font the system fallback picks today; it must never be chosen.
  FakeFontProbe probe({font("MS UI Gothic", false, "msgothic.ttc")});
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray());
}

void TestExportFontResolver::testFallbackSkipsNonAsciiFamilyName() {
  // The HTML patch is byte-level and never transcodes, so a CJK-named family cannot be emitted.
  FakeFontProbe probe({font(QStringLiteral("华文细黑"), true, "stxihei.ttf")});
  QCOMPARE(resolveWkhtmltopdfFonts(probe).m_text, QByteArray());
}

void TestExportFontResolver::testEmptyWhenNoCjkFontIsInstalled() {
  FakeFontProbe probe({font("Segoe UI", false, "segoeui.ttf"), font("Arial", false, "arial.ttf")});
  const auto fonts = resolveWkhtmltopdfFonts(probe);
  QVERIFY(fonts.isEmpty());
}

void TestExportFontResolver::testMonoFallsBackToTheTextFamily() {
  // A Latin-only monospace face would bring the tofu back inside code blocks, so the text family
  // wins over keeping the monospace look.
  FakeFontProbe probe(
      {font("Consolas", false, "consola.ttf"), font("Noto Sans SC", true, "NotoSansSC-VF.ttf")});
  const auto fonts = resolveWkhtmltopdfFonts(probe);
  QCOMPARE(fonts.m_text, QByteArray("Noto Sans SC"));
  QCOMPARE(fonts.m_mono, QByteArray("Noto Sans SC"));
}

void TestExportFontResolver::testMonoNeverTakesAProportionalFallback() {
  // Regression: the generic "any usable CJK family" fallback must NOT apply to the monospace
  // slot. On a real Windows box it resolved to DengXian - a proportional face - which would have
  // silently destroyed code-block alignment. Only the curated monospace list may fill that slot.
  FakeFontProbe probe(
      {font("DengXian", true, "Deng.ttf"), font("Noto Sans SC", true, "NotoSansSC-VF.ttf")});
  const auto fonts = resolveWkhtmltopdfFonts(probe);
  QCOMPARE(fonts.m_text, QByteArray("Noto Sans SC"));
  QCOMPARE(fonts.m_mono, QByteArray("Noto Sans SC"));
  QCOMPARE(resolveWkhtmltopdfFamily(probe, {"Sarasa Mono SC"}, false), QByteArray());
}

void TestExportFontResolver::testMonoIsUsedWhenAvailable() {
  FakeFontProbe probe({font("Noto Sans SC", true, "NotoSansSC-VF.ttf"),
                       font("Sarasa Mono SC", true, "sarasa-mono-sc.ttf")});
  const auto fonts = resolveWkhtmltopdfFonts(probe);
  QCOMPARE(fonts.m_text, QByteArray("Noto Sans SC"));
  QCOMPARE(fonts.m_mono, QByteArray("Sarasa Mono SC"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestExportFontResolver)
#include "test_exportfontresolver.moc"
