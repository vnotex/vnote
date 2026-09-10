#include "outlinecontroller.h"

#include <QSet>
#include <QTimer>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/widgetconfig.h>
#include <models/outlinemodel.h>
#include <views/outlineview.h>
#include <widgets/outlineprovider.h>

using namespace vnotex;

namespace {
struct MovePlan {
  bool m_valid = false;
  int m_sourceHeadingIndex = -1;
  int m_beforeHeadingIndex = -1;
  int m_targetLevel = -1;
  QString m_headingName;
};

MovePlan planMove(const Outline &p_outline, int p_sourceHeadingIndex, int p_targetHeadingIndex,
                  OutlineDropPosition p_position) {
  MovePlan plan;
  if (!p_outline.m_reorderSupported || p_sourceHeadingIndex < 0 ||
      p_sourceHeadingIndex >= p_outline.m_headings.size()) {
    return plan;
  }

  const auto &headings = p_outline.m_headings;
  const auto &source = headings[p_sourceHeadingIndex];
  if (!source.m_reorderable) {
    return plan;
  }

  int sourceEnd = p_sourceHeadingIndex + 1;
  while (sourceEnd < headings.size() && headings[sourceEnd].m_level > source.m_level) {
    ++sourceEnd;
  }

  int beforeHeadingIndex = -1;
  int targetLevel = source.m_level;
  if (p_position == OutlineDropPosition::OnViewport) {
    int rootLevel = 7;
    for (int i = 0; i < headings.size(); ++i) {
      if ((i < p_sourceHeadingIndex || i >= sourceEnd) && headings[i].m_reorderable) {
        rootLevel = qMin(rootLevel, headings[i].m_level);
      }
    }
    targetLevel = rootLevel == 7 ? source.m_level : rootLevel;
  } else {
    if (p_targetHeadingIndex < 0 || p_targetHeadingIndex >= headings.size() ||
        !headings[p_targetHeadingIndex].m_reorderable ||
        (p_targetHeadingIndex >= p_sourceHeadingIndex && p_targetHeadingIndex < sourceEnd)) {
      return plan;
    }

    const auto &target = headings[p_targetHeadingIndex];
    if (p_position == OutlineDropPosition::AboveItem) {
      beforeHeadingIndex = p_targetHeadingIndex;
      targetLevel = target.m_level;
    } else if (p_position == OutlineDropPosition::BelowItem ||
               p_position == OutlineDropPosition::OnItem) {
      int targetEnd = p_targetHeadingIndex + 1;
      while (targetEnd < headings.size() && headings[targetEnd].m_level > target.m_level) {
        ++targetEnd;
      }
      if (targetEnd >= p_sourceHeadingIndex && targetEnd < sourceEnd) {
        targetEnd = sourceEnd;
      }
      while (targetEnd < headings.size() && !headings[targetEnd].m_reorderable) {
        ++targetEnd;
      }
      beforeHeadingIndex = targetEnd < headings.size() ? targetEnd : -1;
      targetLevel = p_position == OutlineDropPosition::OnItem ? target.m_level + 1 : target.m_level;
    } else {
      return plan;
    }
  }

  const int levelDelta = targetLevel - source.m_level;
  for (int i = p_sourceHeadingIndex; i < sourceEnd; ++i) {
    if (headings[i].m_reorderable) {
      const int movedLevel = headings[i].m_level + levelDelta;
      if (movedLevel < 1 || movedLevel > 6) {
        return plan;
      }
    }
  }

  QVector<int> originalOrder;
  QVector<int> movedBlock;
  QVector<int> remainingOrder;
  for (int i = 0; i < headings.size(); ++i) {
    if (!headings[i].m_reorderable) {
      continue;
    }
    originalOrder.append(i);
    if (i >= p_sourceHeadingIndex && i < sourceEnd) {
      movedBlock.append(i);
    } else {
      remainingOrder.append(i);
    }
  }

  int insertion = remainingOrder.size();
  if (beforeHeadingIndex >= 0) {
    insertion = remainingOrder.indexOf(beforeHeadingIndex);
    if (insertion < 0) {
      return plan;
    }
  }
  QVector<int> normalizedOrder = remainingOrder;
  for (int i = 0; i < movedBlock.size(); ++i) {
    normalizedOrder.insert(insertion + i, movedBlock[i]);
  }
  if (normalizedOrder == originalOrder && targetLevel == source.m_level) {
    return plan;
  }

  plan.m_valid = true;
  plan.m_sourceHeadingIndex = p_sourceHeadingIndex;
  plan.m_beforeHeadingIndex = beforeHeadingIndex;
  plan.m_targetLevel = targetLevel;
  plan.m_headingName = source.m_name;
  return plan;
}
} // namespace

OutlineController::OutlineController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  // Create and own the model.
  m_model = new OutlineModel(this);

  // Read initial config values.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    m_autoExpandedLevel = configMgr->getWidgetConfig().getOutlineAutoExpandedLevel();
    m_autoSectionNumberEnabled = configMgr->getWidgetConfig().getOutlineAutoSectionNumberEnabled();
    m_model->setSectionNumberPattern(configMgr->getEditorConfig().getSectionNumberPattern());
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr) {
      m_editorConfigHookId = hookMgr->addAction(
          HookNames::ConfigEditorChanged,
          [this](HookContext &, const QVariantMap &) { updateSectionNumberPattern(); });
    }
  }
  m_model->setAutoSectionNumberEnabled(m_autoSectionNumberEnabled);

  // Setup debounce timer for auto-expand after heading changes.
  m_expandTimer = new QTimer(this);
  m_expandTimer->setSingleShot(true);
  m_expandTimer->setInterval(1000);
  connect(m_expandTimer, &QTimer::timeout, this, [this]() {
    if (!m_view || m_autoExpandedLevel >= 6) {
      return;
    }
    applyExpandLevel();
    // Scroll to the current heading in the view.
    int idx = m_model->getCurrentHeadingIndex();
    if (idx >= 0) {
      m_view->highlightHeading(idx);
    }
  });
}

OutlineController::~OutlineController() {
  if (m_editorConfigHookId != -1) {
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr) {
      hookMgr->removeAction(m_editorConfigHookId);
    }
  }
}

void OutlineController::setView(OutlineView *p_view) {
  // Disconnect from previous view.
  if (m_view) {
    disconnect(m_view, nullptr, this, nullptr);
  }

  m_view = p_view;

  if (m_view) {
    // Wire heading activation: user clicks a heading in the view.
    connect(m_view, &OutlineView::headingActivated, this, [this](int p_headingIndex) {
      if (m_provider) {
        m_provider->headingClicked(p_headingIndex);
      }
      emit focusViewAreaRequested();
    });

    connect(m_view, &OutlineView::itemMoveRequested, this, &OutlineController::requestItemMove);

    m_view->setReorderingEnabled(m_model->isReorderSupported());

    // Apply current expand level to the new view.
    applyExpandLevel();
  }
}

OutlineView *OutlineController::view() const { return m_view; }

OutlineModel *OutlineController::model() const { return m_model; }

void OutlineController::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider) {
  clearPendingReorder();

  // Disconnect from previous provider.
  if (m_provider) {
    disconnect(m_provider.data(), nullptr, this, nullptr);
  }

  m_provider = p_provider;

  if (m_provider) {
    // Connect outline changes.
    connect(m_provider.data(), &OutlineProvider::outlineChanged, this, [this]() {
      clearPendingReorder();
      updateModelFromProvider();
    });

    // Connect current heading changes.
    connect(m_provider.data(), &OutlineProvider::currentHeadingChanged, this, [this]() {
      int idx = m_provider->getCurrentHeadingIndex();
      m_model->setCurrentHeadingIndex(idx);
      if (m_view) {
        m_view->highlightHeading(idx);
      }
      // Start debounce timer for auto-expand.
      m_expandTimer->start();
    });
  }

  // Immediately update model with current provider data (or clear if null).
  updateModelFromProvider();
}

void OutlineController::increaseExpandLevel() {
  if (m_autoExpandedLevel >= 6) {
    return;
  }
  ++m_autoExpandedLevel;

  // Persist to config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getWidgetConfig().setOutlineAutoExpandedLevel(m_autoExpandedLevel);
  }

  applyExpandLevel();
  emit expandLevelChanged(m_autoExpandedLevel);
}

void OutlineController::decreaseExpandLevel() {
  if (m_autoExpandedLevel <= 1) {
    return;
  }
  --m_autoExpandedLevel;

  // Persist to config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getWidgetConfig().setOutlineAutoExpandedLevel(m_autoExpandedLevel);
  }

  applyExpandLevel();
  emit expandLevelChanged(m_autoExpandedLevel);
}

int OutlineController::getExpandLevel() const { return m_autoExpandedLevel; }

void OutlineController::toggleAutoSectionNumber() {
  m_autoSectionNumberEnabled = !m_autoSectionNumberEnabled;

  // Persist to config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getWidgetConfig().setOutlineAutoSectionNumberEnabled(m_autoSectionNumberEnabled);
  }

  // Save expansion state before model reset, then restore after.
  QSet<int> expanded;
  if (m_view) {
    expanded = m_view->saveExpansionState();
  }

  m_model->setAutoSectionNumberEnabled(m_autoSectionNumberEnabled);

  if (m_view) {
    m_view->restoreExpansionState(expanded);
    m_view->highlightHeading(m_model->getCurrentHeadingIndex());
  }
}

void OutlineController::updateSectionNumberPattern() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  QSet<int> expanded;
  if (m_view) {
    expanded = m_view->saveExpansionState();
  }
  m_model->setSectionNumberPattern(configMgr->getEditorConfig().getSectionNumberPattern());
  if (m_view) {
    m_view->restoreExpansionState(expanded);
    m_view->highlightHeading(m_model->getCurrentHeadingIndex());
  }
}

bool OutlineController::isAutoSectionNumberEnabled() const { return m_autoSectionNumberEnabled; }

int OutlineController::getBaseLevel() const {
  if (!m_provider) {
    return 1;
  }
  const auto &outline = m_provider->getOutline();
  if (!outline || outline->m_headings.isEmpty()) {
    return 1;
  }
  return outline->m_headings.first().m_level;
}

void OutlineController::applyExpandLevel() {
  if (m_view) {
    m_view->expandToLevel(m_autoExpandedLevel, getBaseLevel());
  }
}

void OutlineController::requestItemMove(int p_sourceHeadingIndex, int p_targetHeadingIndex,
                                        OutlineDropPosition p_position) {
  clearPendingReorder();
  if (!m_provider) {
    return;
  }

  const auto outline = m_provider->getOutline();
  if (!outline) {
    return;
  }

  const auto plan = planMove(*outline, p_sourceHeadingIndex, p_targetHeadingIndex, p_position);
  if (!plan.m_valid) {
    return;
  }

  m_pendingReorder.m_outline = outline;
  m_pendingReorder.m_sourceHeadingIndex = plan.m_sourceHeadingIndex;
  m_pendingReorder.m_beforeHeadingIndex = plan.m_beforeHeadingIndex;
  m_pendingReorder.m_targetLevel = plan.m_targetLevel;
  emit reorderConfirmationRequested(plan.m_headingName);
}

void OutlineController::confirmReorder(bool p_confirmed) {
  const PendingReorder pending = m_pendingReorder;
  clearPendingReorder();
  if (!p_confirmed || !m_provider || !pending.m_outline ||
      m_provider->getOutline() != pending.m_outline || !pending.m_outline->m_reorderSupported) {
    return;
  }

  m_provider->requestMove(pending.m_sourceHeadingIndex, pending.m_beforeHeadingIndex,
                          pending.m_targetLevel);
}

void OutlineController::clearPendingReorder() { m_pendingReorder = PendingReorder(); }

void OutlineController::updateModelFromProvider() {
  if (m_provider) {
    const auto &outline = m_provider->getOutline();
    m_model->setOutline(outline);

    // Update current heading highlight.
    int idx = m_provider->getCurrentHeadingIndex();
    m_model->setCurrentHeadingIndex(idx);
    if (m_view) {
      m_view->setReorderingEnabled(m_model->isReorderSupported());
      m_view->highlightHeading(idx);
      applyExpandLevel();
    }
  } else {
    // No provider — clear the model.
    m_model->setOutline(QSharedPointer<Outline>());
    m_model->setCurrentHeadingIndex(-1);
    if (m_view) {
      m_view->setReorderingEnabled(false);
    }
  }
}
