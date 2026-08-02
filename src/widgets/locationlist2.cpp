#include "locationlist2.h"

#include <QDebug>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <models/searchresultmodel.h>
#include <utils/widgetutils.h>
#include <views/searchresultdelegate.h>
#include <views/searchresultview.h>

#include "inlinebanner.h"
#include "propertydefs.h"

using namespace vnotex;

LocationList2::LocationList2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
  setupConnections();
}

LocationList2::~LocationList2() = default;

SearchResultModel *LocationList2::getModel() {
  return m_model;
}

void LocationList2::clear() {
  m_model->clear();
}

void LocationList2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  m_truncatedBanner =
      new InlineBanner(InlineBanner::Severity::Warning, tr("Results truncated"), this);
  m_truncatedBanner->hide();
  mainLayout->addWidget(m_truncatedBanner);

  m_stackedWidget = new QStackedWidget(this);

  m_placeholderLabel = new QLabel(tr("No search results"), this);
  m_placeholderLabel->setAlignment(Qt::AlignCenter);
  // Muted, but NOT setEnabled(false): an empty-state message is ordinary
  // information, not an unavailable control, and the themes style QLabel
  // unconditionally so the disabled palette role would never reach it.
  WidgetUtils::setPropertyDynamically(m_placeholderLabel, PropertyDefs::c_mutedText, true);
  m_stackedWidget->addWidget(m_placeholderLabel);

  m_model = new SearchResultModel(this);
  m_view = new SearchResultView(this);
  m_delegate = new SearchResultDelegate(m_view);
  m_view->setItemDelegate(m_delegate);
  m_view->setModel(m_model);
  m_stackedWidget->addWidget(m_view);

  mainLayout->addWidget(m_stackedWidget);

  m_stackedWidget->setCurrentIndex(0);
}

void LocationList2::setupConnections() {
  connect(m_view, &SearchResultView::resultActivated,
          this, &LocationList2::resultActivated);
  connect(m_view, &SearchResultView::contextMenuRequested,
          this, &LocationList2::contextMenuRequested);

  connect(m_model, &QAbstractItemModel::modelReset,
          this, &LocationList2::updatePlaceholder);
}

void LocationList2::updatePlaceholder() {
  bool hasResults = m_model->rowCount() > 0;
  bool truncated = hasResults && m_model->isTruncated();
  qDebug() << "LocationList2::updatePlaceholder: hasResults:" << hasResults
           << "truncated:" << truncated;
  m_stackedWidget->setCurrentIndex(hasResults ? 1 : 0);
  m_truncatedBanner->setVisible(truncated);
}
