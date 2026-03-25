#include "outlinecontroller.h"

#include <QTimer>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
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
    m_sectionNumberEnabled = configMgr->getWidgetConfig().getOutlineSectionNumberEnabled();
  }
  m_model->setSectionNumberEnabled(m_sectionNumberEnabled);

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

OutlineController::~OutlineController() {}

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

    // Apply current expand level to the new view.
    applyExpandLevel();
  }
}

OutlineView *OutlineController::view() const { return m_view; }

OutlineModel *OutlineController::model() const { return m_model; }

void OutlineController::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider) {
  // Disconnect from previous provider.
  if (m_provider) {
    disconnect(m_provider.data(), nullptr, this, nullptr);
  }

  m_provider = p_provider;

  if (m_provider) {
    // Connect outline changes.
    connect(m_provider.data(), &OutlineProvider::outlineChanged, this, [this]() {
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

void OutlineController::toggleSectionNumber() {
  m_sectionNumberEnabled = !m_sectionNumberEnabled;

  // Persist to config.
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getWidgetConfig().setOutlineSectionNumberEnabled(m_sectionNumberEnabled);
  }

  m_model->setSectionNumberEnabled(m_sectionNumberEnabled);
}

bool OutlineController::isSectionNumberEnabled() const { return m_sectionNumberEnabled; }

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

void OutlineController::updateModelFromProvider() {
  if (m_provider) {
    const auto &outline = m_provider->getOutline();
    m_model->setOutline(outline);

    // Apply section number base level from outline data.
    if (outline) {
      m_model->setSectionNumberBaseLevel(outline->m_sectionNumberBaseLevel);
      m_model->setSectionNumberEndingDot(outline->m_sectionNumberEndingDot);
    }

    // Update current heading highlight.
    int idx = m_provider->getCurrentHeadingIndex();
    m_model->setCurrentHeadingIndex(idx);
    if (m_view) {
      m_view->highlightHeading(idx);
      applyExpandLevel();
    }
  } else {
    // No provider — clear the model.
    m_model->setOutline(QSharedPointer<Outline>());
    m_model->setCurrentHeadingIndex(-1);
  }
}
