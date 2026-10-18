#include "zoomtoolbar.h"

#include <QAction>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolBar>

#include <cmath>

#include "propertydefs.h"

using namespace vnotex;

ZoomToolBar::ZoomToolBar(const QIcon &p_zoomOutIcon, const QIcon &p_zoomInIcon, QToolBar *p_toolBar)
    : QObject(p_toolBar) {
  const auto addZoomAction = [this, &p_zoomOutIcon, &p_zoomInIcon, p_toolBar](bool p_zoomIn) {
    const auto iconName =
        p_zoomIn ? QStringLiteral("zoom_in_editor.svg") : QStringLiteral("zoom_out_editor.svg");
    auto *action =
        p_toolBar->addAction(p_zoomIn ? p_zoomInIcon : p_zoomOutIcon,
                             p_zoomIn ? tr("Zoom In") : tr("Zoom Out"));
    action->setProperty("iconName", iconName);
    connect(action, &QAction::triggered, this, [this, p_zoomIn]() {
      if (m_enabled) {
        emit zoomRequested(p_zoomIn);
      }
    });
    return action;
  };
  m_zoomOutAction = addZoomAction(false);
  m_comboBox = new QComboBox(p_toolBar);
  m_comboBox->setObjectName(QStringLiteral("zoomCombo"));
  m_comboBox->setToolTip(tr("Zoom"));
  m_comboBox->setEditable(true);
  m_comboBox->setInsertPolicy(QComboBox::NoInsert);
  m_comboBox->setMinimumContentsLength(8);
  m_comboBox->setCompleter(nullptr);
  m_comboBox->lineEdit()->setProperty(PropertyDefs::c_embeddedLineEdit, true);
  m_comboBox->addItem(tr("Fit Width"), -1.0);
  m_comboBox->addItem(tr("Fit Page"), 0.0);
  for (const int percent : {100, 125, 150, 200}) {
    m_comboBox->addItem(QStringLiteral("%1%").arg(percent), percent / 100.0);
  }
  connect(m_comboBox, QOverload<int>::of(&QComboBox::activated), this, [this](int p_index) {
    if (p_index >= 0 && p_index < m_comboBox->count()) {
      applySelection(m_comboBox->itemData(p_index).toDouble());
    }
  });
  m_comboBox->installEventFilter(this);
  m_comboBox->lineEdit()->installEventFilter(this);
  m_comboAction = p_toolBar->addWidget(m_comboBox);
  m_zoomInAction = addZoomAction(true);
  refreshText();
}

bool ZoomToolBar::eventFilter(QObject *p_object, QEvent *p_event) {
  if ((p_object == m_comboBox || p_object == m_comboBox->lineEdit()) &&
      p_event->type() == QEvent::FocusOut) {
    // Discard drafts before QComboBox can activate a matching preset on focus loss.
    // Cached state is also safe during the owning editor's teardown.
    refreshText();
  } else if (p_object == m_comboBox->lineEdit() && p_event->type() == QEvent::KeyPress) {
    const auto *key = static_cast<QKeyEvent *>(p_event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      // QComboBox can deliver Enter to its line edit twice. Consume it once, and
      // never interpret the truncated display as a new user-entered percentage.
      if (m_comboBox->lineEdit()->isModified()) {
        applyInput();
      }
      p_event->accept();
      return true;
    }
  }
  return QObject::eventFilter(p_object, p_event);
}

void ZoomToolBar::applyInput() {
  QString text = m_comboBox->currentText().trimmed();
  for (int index = 0; index < 2; ++index) {
    if (text.compare(m_comboBox->itemText(index), Qt::CaseInsensitive) == 0) {
      applySelection(m_comboBox->itemData(index).toDouble());
      return;
    }
  }
  if (text.endsWith(QLatin1Char('%'))) {
    text.chop(1);
  }
  bool ok = false;
  const double factor = text.trimmed().toDouble(&ok) / 100.0;
  if (ok && std::isfinite(factor) && factor > 0.0) {
    applySelection(factor);
  } else {
    refreshText();
  }
}

void ZoomToolBar::applySelection(qreal p_zoom) {
  if (m_enabled) {
    emit zoomSelected(p_zoom);
  }
  refreshText();
}

void ZoomToolBar::setZoom(qreal p_zoom) {
  if (m_zoom == p_zoom) {
    return;
  }
  m_zoom = p_zoom;
  refreshText();
}

void ZoomToolBar::refreshText() {
  const QSignalBlocker blocker(m_comboBox);
  int index = -1;
  for (int i = 0; i < m_comboBox->count(); ++i) {
    if (qFuzzyCompare(m_comboBox->itemData(i).toDouble(), m_zoom)) {
      index = i;
      break;
    }
  }
  m_comboBox->setCurrentIndex(index);
  if (m_zoom <= 0.0) {
    m_comboBox->setEditText(m_comboBox->itemText(index));
    return;
  }
  // Ignore floating-point noise at whole percentages; otherwise truncate.
  const qreal percent = m_zoom * 100.0;
  const int nearestPercent = qRound(percent);
  const int displayPercent =
      qFuzzyCompare(percent, qreal(nearestPercent)) ? nearestPercent : static_cast<int>(percent);
  m_comboBox->setEditText(QString::number(displayPercent) + QLatin1Char('%'));
}

void ZoomToolBar::setEnabled(bool p_enabled) {
  m_enabled = p_enabled;
  m_zoomOutAction->setEnabled(p_enabled);
  m_comboAction->setEnabled(p_enabled);
  m_comboBox->setEnabled(p_enabled);
  m_zoomInAction->setEnabled(p_enabled);
}

void ZoomToolBar::setVisible(bool p_visible) {
  m_zoomOutAction->setVisible(p_visible);
  m_comboAction->setVisible(p_visible);
  m_zoomInAction->setVisible(p_visible);
}
