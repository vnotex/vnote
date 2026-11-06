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

OutlineController::OutlineController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  // Create and own the model.
  m_model = new OutlineModel(this);

  // Read initial config values.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    m_autoExpandedLevel = configMgr->getWidgetConfig().getOutlineAutoExpandedLevel();
    m_autoSectionNumberEnabled = configMgr->getWidgetConfig().getOutlineAutoSectionNumberEnabled();
    const auto &editorConfig = configMgr->getEditorConfig();
    m_model->setSectionNumberOptions(editorConfig.getSectionNumberPattern(),
                                     editorConfig.getDetectHeading1ForSectionNumber());
    auto *hookMgr = m_services.get<HookManager>();
    if (hookMgr) {
      m_editorConfigHookId = hookMgr->addAction(
          HookNames::ConfigEditorChanged,
          [this](HookContext &, const QVariantMap &) { updateSectionNumberOptions(); });
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
    connect(m_provider.data(), &OutlineProvider::autoSectionNumberAllowedChanged, this,
            &OutlineController::updateSectionNumberOptions);

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
  updateSectionNumberOptions();
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

  updateSectionNumberOptions();
}

void OutlineController::updateSectionNumberOptions() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  QSet<int> expanded;
  if (m_view) {
    expanded = m_view->saveExpansionState();
  }
  m_model->setAutoSectionNumberEnabled(m_autoSectionNumberEnabled &&
                                       (!m_provider || m_provider->getAutoSectionNumberAllowed()));
  if (configMgr) {
    const auto &editorConfig = configMgr->getEditorConfig();
    m_model->setSectionNumberOptions(editorConfig.getSectionNumberPattern(),
                                     editorConfig.getDetectHeading1ForSectionNumber());
  }
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

  const auto plan = outline->planMove(p_sourceHeadingIndex, p_targetHeadingIndex, p_position);
  if (!plan.isValid()) {
    return;
  }

  m_pendingReorder.m_outline = outline;
  m_pendingReorder.m_move = plan;
  emit reorderConfirmationRequested(outline->m_headings[p_sourceHeadingIndex].m_name);
}

void OutlineController::confirmReorder(bool p_confirmed) {
  const PendingReorder pending = m_pendingReorder;
  clearPendingReorder();
  if (!p_confirmed || !m_provider || !pending.m_outline ||
      m_provider->getOutline() != pending.m_outline || !pending.m_outline->m_reorderSupported) {
    return;
  }

  m_provider->requestMove(pending.m_move);
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
