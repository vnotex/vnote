#ifndef OUTLINEVIEWER_H
#define OUTLINEVIEWER_H

#include <QFrame>
#include <QMap>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QVector>

#include "navigationmodewrapper.h"
#include "outlineprovider.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace vnotex {
class TitleBar;

class OutlineViewer : public QFrame {
  Q_OBJECT
public:
  OutlineViewer(const QString &p_title, QWidget *p_parent = nullptr);

  void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

  NavigationModeWrapper<QTreeWidget, QTreeWidgetItem> *getNavigationModeWrapper();

signals:
  void focusViewArea();

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
  enum Column { Name = 0 };

  void setupUI(const QString &p_title);

  TitleBar *setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

  void updateTree(bool p_force = false);

  void updateOutline(const QSharedPointer<Outline> &p_outline);

  void updateCurrentHeading(int p_idx);

  void highlightHeading(int p_idx);

  void expandTree(int p_level);

  void showLevel();

  // Do not response if m_muted is true.
  void activateItem(QTreeWidgetItem *p_item, bool p_focus = false);

  static void renderTreeAtLevel(const QVector<Outline::Heading> &p_headings, int &p_index,
                                int p_level, QTreeWidget *p_tree, QTreeWidgetItem *p_parentItem,
                                QTreeWidgetItem *p_lastItem, int p_sectionNumberBaseLevel,
                                SectionNumber &p_sectionNumber, bool p_sectionNumberEndingDot);

  static void fillTreeItem(QTreeWidgetItem *p_item, const Outline::Heading &p_heading, int p_index,
                           const QString &p_sectionStr);

  static void updateTreeToOutline(QTreeWidget *p_tree, const Outline &p_outline);

  bool m_muted = false;

  QTimer *m_expandTimer = nullptr;

  QTreeWidget *m_tree = nullptr;

  QSharedPointer<OutlineProvider> m_provider;

  Outline m_outline;

  int m_currentHeadingIndex = -1;

  int m_autoExpandedLevel = 6;

  QScopedPointer<NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>> m_navigationWrapper;
};
} // namespace vnotex

#endif // OUTLINEVIEWER_H
