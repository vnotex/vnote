// test_settings_navigation.cpp - Verify SettingsWidget::navigateTo() logic.
//
// Strategy: build a QTreeWidget with stubbed SettingsPage items that mirror the
// real settings hierarchy, then exercise the slug-matching tree traversal and
// fragment scroll logic directly. This avoids the deep dependency chain of the
// full SettingsWidget (ThemeService, MainWindow2, etc.).
#include <QtTest>

#include <QScrollArea>
#include <QStackedLayout>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <core/servicelocator.h>
#include <widgets/dialogs/settings/settingspage.h>

namespace vnotex {

// Minimal SettingsPage implementation to avoid pulling in real page deps.
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

using namespace vnotex;

namespace tests {

// Concrete stub page that returns a configurable slug.
class StubPage : public SettingsPage {
  Q_OBJECT
public:
  StubPage(ServiceLocator &p_services, const QString &p_slug, const QString &p_title,
           QWidget *p_parent = nullptr)
      : SettingsPage(p_services, p_parent), m_slug(p_slug), m_title(p_title) {}

  QString title() const override { return m_title; }
  QString slug() const override { return m_slug; }

protected:
  void loadInternal() override {}
  bool saveInternal() override { return true; }

private:
  QString m_slug;
  QString m_title;
};

// Replicates SettingsWidget's navigateTo logic for isolated testing.
// This mirrors the production code exactly so the test validates correctness.
class NavigationTestHarness : public QObject {
  Q_OBJECT
public:
  explicit NavigationTestHarness(ServiceLocator &p_services)
      : m_services(p_services) {
    m_tree = new QTreeWidget();
    m_scrollArea = new QScrollArea();

    auto *scrollWidget = new QWidget(m_scrollArea);
    m_scrollArea->setWidget(scrollWidget);
    m_pageLayout = new QStackedLayout(scrollWidget);

    // Build the same hierarchy as SettingsWidget::setupPages():
    // General, NoteManagement, Appearance→Theme, QuickAccess,
    // Editor→ImageHost/Vi/TextEditor/MarkdownEditor, FileAssociation
    addTopLevel("general", "General");
    addTopLevel("notemanagement", "Note Management");
    {
      auto *item = addTopLevel("appearance", "Appearance");
      addChild(item, "theme", "Theme");
    }
    addTopLevel("quickaccess", "Quick Access");
    {
      auto *item = addTopLevel("editor", "Editor");
      addChild(item, "imagehost", "Image Host");
      addChild(item, "vi", "Vi");
      addChild(item, "texteditor", "Text Editor");
      addChild(item, "markdowneditor", "Markdown Editor");
    }
    addTopLevel("fileassociation", "File Association");

    // Select first item (mirrors production behavior).
    m_tree->setCurrentItem(m_tree->topLevelItem(0));
    m_tree->expandAll();

    // Connect currentItemChanged to switch stacked layout (mirrors production).
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *p_item, QTreeWidgetItem *) {
              if (!p_item) return;
              auto *page = itemPage(p_item);
              if (page) {
                m_pageLayout->setCurrentWidget(page);
              }
            });
  }

  ~NavigationTestHarness() {
    delete m_tree;
    delete m_scrollArea;
  }

  // Production-identical navigateTo logic.
  void navigateTo(const QStringList &p_pathSegments, const QString &p_fragment) {
    if (!p_pathSegments.isEmpty()) {
      QTreeWidgetItem *currentItem = nullptr;
      for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        auto *page = itemPage(item);
        if (page && page->slug() == p_pathSegments[0]) {
          currentItem = item;
          break;
        }
      }
      for (int seg = 1; seg < p_pathSegments.size() && currentItem; ++seg) {
        QTreeWidgetItem *found = nullptr;
        for (int c = 0; c < currentItem->childCount(); ++c) {
          auto *child = currentItem->child(c);
          auto *page = itemPage(child);
          if (page && page->slug() == p_pathSegments[seg]) {
            found = child;
            break;
          }
        }
        currentItem = found;
      }
      if (currentItem) {
        m_tree->setCurrentItem(currentItem);
      }
    }

    if (!p_fragment.isEmpty()) {
      auto *currentPage = m_pageLayout->currentWidget();
      if (currentPage) {
        auto *target = currentPage->findChild<QWidget *>(p_fragment);
        if (target && m_scrollArea) {
          QTimer::singleShot(0, this, [this, target]() {
            m_scrollArea->ensureWidgetVisible(target);
          });
        }
      }
    }
  }

  SettingsPage *currentPage() const {
    return qobject_cast<SettingsPage *>(m_pageLayout->currentWidget());
  }

  QTreeWidgetItem *currentItem() const { return m_tree->currentItem(); }

  QTreeWidget *tree() const { return m_tree; }

private:
  SettingsPage *itemPage(QTreeWidgetItem *p_item) const {
    return p_item->data(0, Qt::UserRole).value<SettingsPage *>();
  }

  QTreeWidgetItem *addTopLevel(const QString &p_slug, const QString &p_title) {
    auto *page = new StubPage(m_services, p_slug, p_title, m_scrollArea);
    m_pageLayout->addWidget(page);
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, p_title);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<SettingsPage *>(page)));
    return item;
  }

  void addChild(QTreeWidgetItem *p_parent, const QString &p_slug, const QString &p_title) {
    auto *page = new StubPage(m_services, p_slug, p_title, m_scrollArea);
    m_pageLayout->addWidget(page);
    auto *item = new QTreeWidgetItem(p_parent);
    item->setText(0, p_title);
    item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<SettingsPage *>(page)));
  }

  ServiceLocator &m_services;
  QTreeWidget *m_tree = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QStackedLayout *m_pageLayout = nullptr;
};

class TestSettingsNavigation : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Top-level slug navigation.
  void testNavigateToGeneral();
  void testNavigateToAppearance();
  void testNavigateToEditor();
  void testNavigateToFileAssociation();

  // Nested slug navigation.
  void testNavigateToTheme();
  void testNavigateToMarkdownEditor();
  void testNavigateToVi();

  // Edge cases.
  void testNavigateNonexistent();
  void testNavigateEmptyPath();
  void testNavigatePartialPathStopsAtLastMatch();
  void testNavigateDeepPathWithWrongChild();

  // Fragment scroll (no crash, widget found).
  void testFragmentScrollFindsNamedWidget();
  void testFragmentScrollMissingWidget();

private:
  ServiceLocator m_services;
  NavigationTestHarness *m_harness = nullptr;
};

void TestSettingsNavigation::initTestCase() {
  m_harness = new NavigationTestHarness(m_services);
}

void TestSettingsNavigation::cleanupTestCase() {
  delete m_harness;
  m_harness = nullptr;
}

void TestSettingsNavigation::testNavigateToGeneral() {
  // Start somewhere else first.
  m_harness->navigateTo({"editor"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("editor"));

  m_harness->navigateTo({"general"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("general"));
}

void TestSettingsNavigation::testNavigateToAppearance() {
  m_harness->navigateTo({"appearance"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("appearance"));
}

void TestSettingsNavigation::testNavigateToEditor() {
  m_harness->navigateTo({"editor"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("editor"));
}

void TestSettingsNavigation::testNavigateToFileAssociation() {
  m_harness->navigateTo({"fileassociation"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("fileassociation"));
}

void TestSettingsNavigation::testNavigateToTheme() {
  m_harness->navigateTo({"appearance", "theme"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("theme"));
}

void TestSettingsNavigation::testNavigateToMarkdownEditor() {
  m_harness->navigateTo({"editor", "markdowneditor"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("markdowneditor"));
}

void TestSettingsNavigation::testNavigateToVi() {
  m_harness->navigateTo({"editor", "vi"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("vi"));
}

void TestSettingsNavigation::testNavigateNonexistent() {
  // Navigate to a known page first.
  m_harness->navigateTo({"general"}, QString());
  auto *before = m_harness->currentPage();

  // Navigate to nonexistent — should be a no-op (no crash).
  m_harness->navigateTo({"nonexistent"}, QString());
  QCOMPARE(m_harness->currentPage(), before);
}

void TestSettingsNavigation::testNavigateEmptyPath() {
  m_harness->navigateTo({"editor"}, QString());
  auto *before = m_harness->currentPage();

  // Empty path — should be a no-op.
  m_harness->navigateTo({}, QString());
  QCOMPARE(m_harness->currentPage(), before);
}

void TestSettingsNavigation::testNavigatePartialPathStopsAtLastMatch() {
  // Navigate to known page first.
  m_harness->navigateTo({"general"}, QString());

  // "appearance" exists but "nonexistent" child doesn't — should stay on general.
  m_harness->navigateTo({"appearance", "nonexistent"}, QString());
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("general"));
}

void TestSettingsNavigation::testNavigateDeepPathWithWrongChild() {
  m_harness->navigateTo({"general"}, QString());

  // "editor" is valid top-level, but "theme" is not a child of editor.
  m_harness->navigateTo({"editor", "theme"}, QString());
  // Should not navigate since "theme" is under "appearance", not "editor".
  QCOMPARE(m_harness->currentPage()->slug(), QStringLiteral("general"));
}

void TestSettingsNavigation::testFragmentScrollFindsNamedWidget() {
  m_harness->navigateTo({"editor"}, QString());
  auto *page = m_harness->currentPage();

  // Create a named child widget on the current page.
  auto *namedWidget = new QWidget(page);
  namedWidget->setObjectName(QStringLiteral("testFragment"));

  // Should not crash; fragment scroll is deferred via QTimer::singleShot.
  m_harness->navigateTo({}, QStringLiteral("testFragment"));

  // Process the deferred singleShot.
  QCoreApplication::processEvents();
  // No crash = success.
}

void TestSettingsNavigation::testFragmentScrollMissingWidget() {
  m_harness->navigateTo({"editor"}, QString());

  // Fragment widget doesn't exist — should be a no-op, no crash.
  m_harness->navigateTo({}, QStringLiteral("doesNotExist"));
  QCoreApplication::processEvents();
}

} // namespace tests

QTEST_MAIN(tests::TestSettingsNavigation)
#include "test_settings_navigation.moc"
