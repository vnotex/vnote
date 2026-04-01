#ifndef NAVIGATIONMODEVIEWWRAPPER_H
#define NAVIGATIONMODEVIEWWRAPPER_H

#include "navigationmode.h"

#include <QAbstractItemView>
#include <QLabel>
#include <QModelIndex>
#include <QScrollBar>

namespace vnotex {
template <typename T> class NavigationModeViewWrapper : public NavigationMode {
public:
  explicit NavigationModeViewWrapper(T *p_widget, ThemeService *p_themeService = nullptr);

protected:
  QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

  void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

  void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

  void clearNavigation() Q_DECL_OVERRIDE;

private:
  T *m_widget = nullptr;

  QVector<QModelIndex> m_navigationIndexes;
};

template <typename T>
NavigationModeViewWrapper<T>::NavigationModeViewWrapper(T *p_widget,
                                                        ThemeService *p_themeService)
    : NavigationMode(NavigationMode::Type::DoubleKeys, p_widget, p_themeService),
      m_widget(p_widget) {}

template <typename T> QVector<void *> NavigationModeViewWrapper<T>::getVisibleNavigationItems() {
  m_navigationIndexes.clear();

  QVector<void *> items;
  if (!m_widget || !m_widget->model()) {
    return items;
  }

  const QRect viewportRect = m_widget->viewport()->rect();
  if (!viewportRect.isValid() || viewportRect.isEmpty()) {
    return items;
  }

  int y = viewportRect.top();
  const int probeX = viewportRect.left() + 1;

  while (y <= viewportRect.bottom() && m_navigationIndexes.size() < c_maxNumOfNavigationItems) {
    const QModelIndex index = m_widget->indexAt(QPoint(probeX, y));
    if (!index.isValid()) {
      ++y;
      continue;
    }

    const QRect rect = m_widget->visualRect(index);
    if (!rect.isValid()) {
      ++y;
      continue;
    }

    if (viewportRect.intersects(rect)) {
      bool found = false;
      for (const auto &it : m_navigationIndexes) {
        if (it == index) {
          found = true;
          break;
        }
      }

      if (!found) {
        m_navigationIndexes.append(index);
      }
    }

    const int nextY = rect.bottom() + 1;
    y = nextY > y ? nextY : (y + 1);
  }

  items.reserve(m_navigationIndexes.size());
  for (int i = 0; i < m_navigationIndexes.size(); ++i) {
    items.push_back(&m_navigationIndexes[i]);
  }

  return items;
}

template <typename T>
void NavigationModeViewWrapper<T>::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) {
  Q_UNUSED(p_idx);
  Q_ASSERT(p_item);
  Q_ASSERT(p_label);

  int extraWidth = p_label->width() + 2;
  auto vbar = m_widget->verticalScrollBar();
  if (vbar && vbar->minimum() != vbar->maximum()) {
    extraWidth += vbar->width();
  }

  const auto idx = static_cast<QModelIndex *>(p_item);
  const auto rt = m_widget->visualRect(*idx);
  const int x = rt.x() + m_widget->width() - extraWidth;
  const int y = rt.y();
  p_label->move(x, y);
}

template <typename T> void NavigationModeViewWrapper<T>::handleTargetHit(void *p_item) {
  Q_ASSERT(p_item);

  const auto idx = static_cast<QModelIndex *>(p_item);
  m_widget->setCurrentIndex(*idx);
  m_widget->setFocus();
}

template <typename T> void NavigationModeViewWrapper<T>::clearNavigation() {
  m_navigationIndexes.clear();
  NavigationMode::clearNavigation();
}
} // namespace vnotex

#endif // NAVIGATIONMODEVIEWWRAPPER_H
