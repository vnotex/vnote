#include <QLabel>
#include <QTreeWidget>

#include <unitedentry/entrywidgetfactory.h>
#include <widgets/treewidget.h>

using namespace vnotex;

// Lightweight EntryWidgetFactory backed by plain Qt widgets so the "task" entry
// can build a real QTreeWidget / QLabel under a QApplication without pulling in
// the enhanced TreeWidget dependencies.
QSharedPointer<QTreeWidget> EntryWidgetFactory::createTreeWidget(int p_columnCount) {
  auto tree = QSharedPointer<QTreeWidget>::create();
  tree->setColumnCount(p_columnCount);
  tree->setHeaderHidden(true);
  return tree;
}

QSharedPointer<QLabel> EntryWidgetFactory::createLabel(const QString &p_info) {
  return QSharedPointer<QLabel>::create(p_info);
}

// --- TreeWidget stubs (referenced by IUnitedEntry::handleActionCommon) ---
void TreeWidget::selectParentItem(QTreeWidget *) {}

bool TreeWidget::isExpanded(const QTreeWidget *) { return false; }
