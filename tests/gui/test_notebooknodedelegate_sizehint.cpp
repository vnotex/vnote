// Tests for NotebookNodeDelegate::sizeHint(): the width must be derived from the
// node name (and the child-count badge) so a content-sized column can show the
// full label instead of a hardcoded 200px.

#include <QtTest>

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTreeView>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <gui/utils/treeviewutils.h>
#include <models/inodelistmodel.h>
#include <views/notebooknodedelegate.h>

namespace tests {

class TestNotebookNodeDelegateSizeHint : public QObject {
  Q_OBJECT

private slots:
  void testLongNameIsWiderThanLegacyFixedWidth();
  void testChildCountBadgeAddsWidth();
  void fullNameFitsBesideBadgeAtMeasuredWidth();
  void testCompactContentMatchesStandardDelegate_data();
  void testCompactContentMatchesStandardDelegate();

private:
  static QModelIndex makeIndex(QStandardItemModel &p_model, const vnotex::NodeInfo &p_info);
  static int firstInkColumn(QStyledItemDelegate &p_delegate, const QStyleOptionViewItem &p_option,
                            const QModelIndex &p_index);
};

QModelIndex TestNotebookNodeDelegateSizeHint::makeIndex(QStandardItemModel &p_model,
                                                        const vnotex::NodeInfo &p_info) {
  auto *item = new QStandardItem(p_info.name);
  item->setData(QVariant::fromValue(p_info), vnotex::INodeListModel::NodeInfoRole);
  p_model.appendRow(item);
  return p_model.index(p_model.rowCount() - 1, 0);
}

void TestNotebookNodeDelegateSizeHint::testLongNameIsWiderThanLegacyFixedWidth() {
  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate delegate(services);

  QStandardItemModel model;
  vnotex::NodeInfo info;
  info.id.notebookId = QStringLiteral("nb");
  info.id.relativePath = QStringLiteral("a.md");
  info.isFolder = false;
  info.name = QString(120, QLatin1Char('W'));
  const QModelIndex idx = makeIndex(model, info);

  QStyleOptionViewItem opt;
  const QSize hint = delegate.sizeHint(opt, idx);

  // The old implementation always returned 200.
  QVERIFY(hint.width() > 200);
  QVERIFY(hint.height() > 0);
}

void TestNotebookNodeDelegateSizeHint::testChildCountBadgeAddsWidth() {
  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate delegate(services);

  QStandardItemModel model;

  vnotex::NodeInfo plain;
  plain.id.notebookId = QStringLiteral("nb");
  plain.id.relativePath = QStringLiteral("MyFolder");
  plain.isFolder = true;
  plain.name = QStringLiteral("MyFolder");
  plain.childCount = 0;

  vnotex::NodeInfo badged = plain;
  badged.childCount = 42;

  QStyleOptionViewItem opt;
  const int plainWidth = delegate.sizeHint(opt, makeIndex(model, plain)).width();
  const int badgedWidth = delegate.sizeHint(opt, makeIndex(model, badged)).width();

  QVERIFY(badgedWidth > plainWidth);
}

void TestNotebookNodeDelegateSizeHint::fullNameFitsBesideBadgeAtMeasuredWidth() {
  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate delegate(services);
  QStandardItemModel model;
  vnotex::NodeInfo folder;
  folder.id.notebookId = QStringLiteral("nb");
  folder.id.relativePath = QStringLiteral("folder");
  folder.isFolder = true;
  folder.childCount = 42;
  folder.name =
      QStringLiteral("A very long folder name that needs horizontal scrolling to see its ending");
  folder.textColor = QStringLiteral("black");
  const auto index = makeIndex(model, folder);
  QStyleOptionViewItem option;
  option.font = QApplication::font();
  option.fontMetrics = QFontMetrics(option.font);
  const QSize hint = delegate.sizeHint(option, index);
  const auto render = [&](int p_width) {
    QImage image(p_width, hint.height(), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    option.rect = image.rect();
    delegate.paint(&painter, option, index);
    return image;
  };
  const int nameWidth = qCeil(QFontMetricsF(option.font).horizontalAdvance(folder.name));
  const QRect label(0, 0, nameWidth + 8, hint.height());
  QCOMPARE(render(hint.width()).copy(label), render(hint.width() + 400).copy(label));
}

int TestNotebookNodeDelegateSizeHint::firstInkColumn(QStyledItemDelegate &p_delegate,
                                                     const QStyleOptionViewItem &p_option,
                                                     const QModelIndex &p_index) {
  QImage image(p_option.rect.size(), QImage::Format_RGB32);
  image.fill(Qt::white); // Test canvas, not application chrome.
  {
    QPainter painter(&image);
    p_delegate.paint(&painter, p_option, p_index);
  }
  for (int x = 0; x < image.width(); ++x) {
    for (int y = 0; y < image.height(); ++y) {
      if (qGray(image.pixel(x, y)) < 215) {
        return x;
      }
    }
  }
  return -1;
}

void TestNotebookNodeDelegateSizeHint::testCompactContentMatchesStandardDelegate_data() {
  QTest::addColumn<int>("leftBorder");
  QTest::newRow("padding-only") << 0;
  QTest::newRow("left-accent-border") << 2;
}

void TestNotebookNodeDelegateSizeHint::testCompactContentMatchesStandardDelegate() {
  QFETCH(int, leftBorder);
  QTreeView view;
  view.setStyleSheet(QStringLiteral("QTreeView::item { padding: 4px 4px; "
                                    "border-left: %1px solid transparent; "
                                    "color: black; background: white; }")
                         .arg(leftBorder));
  view.ensurePolished();
  vnotex::TreeViewUtils::applyThemeSpacing(&view, true);

  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate notebook(services);
  QStyledItemDelegate standard;
  QStandardItemModel model;
  vnotex::NodeInfo folder;
  folder.id.notebookId = QStringLiteral("nb");
  folder.id.relativePath = QStringLiteral("folder");
  folder.isFolder = true;
  folder.name = QStringLiteral("MMMM");
  folder.textColor = QStringLiteral("black");
  const auto folderIndex = makeIndex(model, folder);
  view.setModel(&model);

  QStyleOptionViewItem empty;
  empty.initFrom(&view);
  empty.font = view.font();
  empty.widget = &view;
  empty.state |= QStyle::State_Enabled;
  empty.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected | QStyle::State_MouseOver);
  empty.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
  QStyleOptionViewItem paintOption(empty);
  paintOption.rect = QRect(0, 0, 320, notebook.sizeHint(empty, folderIndex).height());

  const int folderInk = firstInkColumn(notebook, paintOption, folderIndex);
  const int standardInk = firstInkColumn(standard, paintOption, folderIndex);
  QVERIFY(folderInk >= 0 && standardInk >= 0);
  QVERIFY2(qAbs(folderInk - standardInk) <= 1,
           qPrintable(QStringLiteral("Notebook text starts at %1; standard tree text starts at %2")
                          .arg(folderInk)
                          .arg(standardInk)));
  const int folderWidth = notebook.sizeHint(empty, folderIndex).width();
  QCOMPARE(notebook.sizeHint(paintOption, folderIndex).width(), folderWidth);
  QVERIFY(qAbs(folderWidth - standard.sizeHint(empty, folderIndex).width()) <= 1);

  auto file = folder;
  file.isFolder = false;
  file.id.relativePath = QStringLiteral("file.md");
  const auto fileIndex = makeIndex(model, file);
  const int fileInk = firstInkColumn(notebook, paintOption, fileIndex);
  QVERIFY(fileInk >= 0);
  // Preserve the existing inclusive QRect::right() convention and reserved icon slot.
  QCOMPARE(fileInk - folderInk, 16 - 1 + 4);
  QCOMPARE(notebook.sizeHint(empty, fileIndex).width() - folderWidth, 16 + 4);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookNodeDelegateSizeHint)
#include "test_notebooknodedelegate_sizehint.moc"
