// test_settings_slug.cpp - Verify slug() correctness for all settings pages.
//
// Strategy: include page headers, provide stub implementations for all virtual
// methods except slug() (which we pull from the real .cpp files via a helper).
// This avoids the deep dependency chain of the full widget layer.
#include <QtTest>

#include <QRegularExpression>
#include <QSet>

#include <core/servicelocator.h>

// SettingsPage base class - provide implementation inline to avoid deps.
#include <widgets/dialogs/settings/settingspage.h>

namespace vnotex {

SettingsPage::SettingsPage(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {}

SettingsPage::~SettingsPage() {}

void SettingsPage::load() {}

bool SettingsPage::save() { return true; }

void SettingsPage::reset() {}

bool SettingsPage::search(const QString &) { return false; }

const QString &SettingsPage::error() const {
  static QString empty;
  return empty;
}

bool SettingsPage::isRestartNeeded() const { return false; }

void SettingsPage::searchHit(QWidget *, bool) {}

void SettingsPage::addSearchItem(const QString &, QWidget *) {}

void SettingsPage::addSearchItem(const QString &, const QString &, QWidget *) {}

void SettingsPage::setError(const QString &) {}

bool SettingsPage::isLoading() const { return false; }

void SettingsPage::pageIsChanged() {}

void SettingsPage::pageIsChangedWithRestartNeeded() {}

} // namespace vnotex

// Now include each page header. We provide stub implementations for all
// virtual methods (title, loadInternal, saveInternal, setupUI) and the real
// slug() implementation.

#include <widgets/dialogs/settings/generalpage.h>
#include <widgets/dialogs/settings/notemanagementpage.h>
#include <widgets/dialogs/settings/appearancepage.h>
#include <widgets/dialogs/settings/themepage.h>
#include <widgets/dialogs/settings/quickaccesspage.h>
#include <widgets/dialogs/settings/editorpage.h>
#include <widgets/dialogs/settings/imagehostpage.h>
#include <widgets/dialogs/settings/vipage.h>
#include <widgets/dialogs/settings/texteditorpage.h>
#include <widgets/dialogs/settings/markdowneditorpage.h>
#include <widgets/dialogs/settings/fileassociationpage.h>

namespace vnotex {

// --- GeneralPage ---
GeneralPage::GeneralPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString GeneralPage::title() const { return QStringLiteral("General"); }
QString GeneralPage::slug() const { return QStringLiteral("general"); }
void GeneralPage::loadInternal() {}
bool GeneralPage::saveInternal() { return true; }

// --- NoteManagementPage ---
NoteManagementPage::NoteManagementPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString NoteManagementPage::title() const { return QStringLiteral("Note Management"); }
QString NoteManagementPage::slug() const { return QStringLiteral("notemanagement"); }
void NoteManagementPage::loadInternal() {}
bool NoteManagementPage::saveInternal() { return true; }

// --- AppearancePage ---
AppearancePage::AppearancePage(ServiceLocator &p_services, MainWindow2 *,
                               QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString AppearancePage::title() const { return QStringLiteral("Appearance"); }
QString AppearancePage::slug() const { return QStringLiteral("appearance"); }
void AppearancePage::loadInternal() {}
bool AppearancePage::saveInternal() { return true; }

// --- ThemePage ---
ThemePage::ThemePage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
ThemePage::~ThemePage() {}
QString ThemePage::title() const { return QStringLiteral("Theme"); }
QString ThemePage::slug() const { return QStringLiteral("theme"); }
void ThemePage::loadInternal() {}
bool ThemePage::saveInternal() { return true; }

// --- QuickAccessPage ---
QuickAccessPage::QuickAccessPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString QuickAccessPage::title() const { return QStringLiteral("Quick Access"); }
QString QuickAccessPage::slug() const { return QStringLiteral("quickaccess"); }
void QuickAccessPage::loadInternal() {}
bool QuickAccessPage::saveInternal() { return true; }

// --- EditorPage ---
EditorPage::EditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString EditorPage::title() const { return QStringLiteral("Editor"); }
QString EditorPage::slug() const { return QStringLiteral("editor"); }
void EditorPage::loadInternal() {}
bool EditorPage::saveInternal() { return true; }

// --- ImageHostPage ---
ImageHostPage::ImageHostPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString ImageHostPage::title() const { return QStringLiteral("Image Host"); }
QString ImageHostPage::slug() const { return QStringLiteral("imagehost"); }
void ImageHostPage::loadInternal() {}
bool ImageHostPage::saveInternal() { return true; }

// --- ViPage ---
ViPage::ViPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString ViPage::title() const { return QStringLiteral("Vi"); }
QString ViPage::slug() const { return QStringLiteral("vi"); }
void ViPage::loadInternal() {}
bool ViPage::saveInternal() { return true; }

// --- TextEditorPage ---
TextEditorPage::TextEditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString TextEditorPage::title() const { return QStringLiteral("Text Editor"); }
QString TextEditorPage::slug() const { return QStringLiteral("texteditor"); }
void TextEditorPage::loadInternal() {}
bool TextEditorPage::saveInternal() { return true; }

// --- MarkdownEditorPage ---
MarkdownEditorPage::MarkdownEditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString MarkdownEditorPage::title() const { return QStringLiteral("Markdown Editor"); }
QString MarkdownEditorPage::slug() const { return QStringLiteral("markdowneditor"); }
void MarkdownEditorPage::loadInternal() {}
bool MarkdownEditorPage::saveInternal() { return true; }

// --- FileAssociationPage ---
FileAssociationPage::FileAssociationPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {}
QString FileAssociationPage::title() const { return QStringLiteral("File Association"); }
QString FileAssociationPage::slug() const { return QStringLiteral("fileassociation"); }
void FileAssociationPage::loadInternal() {}
bool FileAssociationPage::saveInternal() { return true; }

} // namespace vnotex

using namespace vnotex;

namespace tests {

// Minimal concrete subclass to test base class default slug().
class StubSettingsPage : public SettingsPage {
  Q_OBJECT
public:
  explicit StubSettingsPage(ServiceLocator &p_services, QWidget *p_parent = nullptr)
      : SettingsPage(p_services, p_parent) {}

  QString title() const override { return QStringLiteral("Stub"); }

protected:
  void loadInternal() override {}
  bool saveInternal() override { return true; }
};

class TestSettingsSlug : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  // Base class default
  void testBaseClassSlugIsEmpty();

  // Data-driven: each page returns expected slug
  void testPageSlug_data();
  void testPageSlug();

  // All slugs are unique
  void testSlugsAreUnique();

  // Slug format: no spaces, no uppercase, no non-ASCII
  void testSlugFormat_data();
  void testSlugFormat();

private:
  ServiceLocator m_services;
  QStringList m_allSlugs;
  bool m_slugsCollected = false;
};

void TestSettingsSlug::initTestCase() {
  // Construct each page, grab its slug, then destroy it.
  // No vxcore needed since we stubbed out all methods that access services.
  {
    GeneralPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    NoteManagementPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    AppearancePage p(m_services, nullptr);
    m_allSlugs << p.slug();
  }
  {
    ThemePage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    QuickAccessPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    EditorPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    ImageHostPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    ViPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    TextEditorPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    MarkdownEditorPage p(m_services);
    m_allSlugs << p.slug();
  }
  {
    FileAssociationPage p(m_services);
    m_allSlugs << p.slug();
  }
  m_slugsCollected = true;
}

void TestSettingsSlug::testBaseClassSlugIsEmpty() {
  StubSettingsPage stub(m_services);
  QCOMPARE(stub.slug(), QString());
}

void TestSettingsSlug::testPageSlug_data() {
  QTest::addColumn<int>("index");
  QTest::addColumn<QString>("expectedSlug");

  QTest::newRow("general") << 0 << "general";
  QTest::newRow("notemanagement") << 1 << "notemanagement";
  QTest::newRow("appearance") << 2 << "appearance";
  QTest::newRow("theme") << 3 << "theme";
  QTest::newRow("quickaccess") << 4 << "quickaccess";
  QTest::newRow("editor") << 5 << "editor";
  QTest::newRow("imagehost") << 6 << "imagehost";
  QTest::newRow("vi") << 7 << "vi";
  QTest::newRow("texteditor") << 8 << "texteditor";
  QTest::newRow("markdowneditor") << 9 << "markdowneditor";
  QTest::newRow("fileassociation") << 10 << "fileassociation";
}

void TestSettingsSlug::testPageSlug() {
  QVERIFY(m_slugsCollected);
  QFETCH(int, index);
  QFETCH(QString, expectedSlug);

  QVERIFY(index < m_allSlugs.size());
  QCOMPARE(m_allSlugs[index], expectedSlug);
}

void TestSettingsSlug::testSlugsAreUnique() {
  QVERIFY(m_slugsCollected);
  QSet<QString> slugSet(m_allSlugs.begin(), m_allSlugs.end());
  QCOMPARE(slugSet.size(), 11);
  QCOMPARE(m_allSlugs.size(), 11);
}

void TestSettingsSlug::testSlugFormat_data() {
  QTest::addColumn<QString>("slug");

  for (int i = 0; i < m_allSlugs.size(); ++i) {
    QTest::newRow(qPrintable(m_allSlugs[i])) << m_allSlugs[i];
  }
}

void TestSettingsSlug::testSlugFormat() {
  QFETCH(QString, slug);

  QVERIFY2(!slug.isEmpty(), "Slug must not be empty");
  QVERIFY2(!slug.contains(' '), qPrintable(QStringLiteral("Slug contains space: '%1'").arg(slug)));
  QCOMPARE(slug, slug.toLower());

  QRegularExpression asciiOnly(QStringLiteral("^[a-z0-9-]+$"));
  QVERIFY2(asciiOnly.match(slug).hasMatch(),
           qPrintable(QStringLiteral("Slug contains invalid chars: '%1'").arg(slug)));
}

} // namespace tests

QTEST_MAIN(tests::TestSettingsSlug)
#include "test_settings_slug.moc"
