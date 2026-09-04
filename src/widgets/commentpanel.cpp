#include "commentpanel.h"

#include <QComboBox>
// Explicit: the free functions below call QCoreApplication::translate()
// directly (they are not members, so tr() is unavailable). Qt 6's widget
// headers happen to pull this in transitively; Qt 5.15's do not.
#include <QCoreApplication>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/utils/widgetutils.h>

#include "commentprovider.h"
#include "propertydefs.h"

using namespace vnotex;

namespace {
// One intent per typing burst rather than per keystroke.
constexpr int c_textDebounceMs = 300;

constexpr int c_idRole = Qt::UserRole + 1;

// Longest quoted-text preview shown in the editor pane. An anchor may carry up
// to PdfQuadsAnchor::maxAnchorTextLength() (4096) characters, and a word-wrapped
// QLabel's minimumSizeHint GROWS WITH ITS CONTENT -- which overrides the layout
// stretch factors, so a large highlight would push the comment list off the
// dock entirely. The full text stays available as the tooltip.
constexpr int c_maxAnchorPreviewChars = 140;

// Hard backstop for the same problem, independent of the preview length: in a
// very narrow dock even a short preview wraps to many lines. Expressed in text
// lines so it follows the theme font and DPI.
constexpr int c_maxAnchorPreviewLines = 4;

QString truncated(const QString &p_text, int p_maxChars) {
  if (p_text.size() <= p_maxChars) {
    return p_text;
  }
  return p_text.left(p_maxChars).trimmed() + QStringLiteral("\u2026");
}

// What to show for a comment with no body of its own. Ink and text boxes have
// no quoted text, so without a per-type placeholder they would all read
// "(highlight)".
QString anchorPlaceholder(const QJsonObject &p_anchor) {
  const auto type = p_anchor.value(QStringLiteral("type")).toString();
  if (type == PdfInkAnchor::type()) {
    return QCoreApplication::translate("CommentPanel", "(drawing)");
  }
  if (type == PdfFreeTextAnchor::type()) {
    return QCoreApplication::translate("CommentPanel", "(empty text box)");
  }
  if (type == PdfQuadsAnchor::type()) {
    return QCoreApplication::translate("CommentPanel", "(highlight)");
  }
  return QCoreApplication::translate("CommentPanel", "(unsupported annotation)");
}

// One-line preview for the list row.
QString rowLabel(const Comment &p_comment) {
  const int page = anchorPage(p_comment.m_anchor);
  QString prefix =
      page >= 0 ? QCoreApplication::translate("CommentPanel", "p.%1 ").arg(page + 1) : QString();

  QString body = p_comment.m_text.trimmed();
  if (body.isEmpty()) {
    // Only a highlight has quoted text to fall back on; ink and text boxes fall
    // through to the per-type placeholder below.
    body = PdfQuadsAnchor::text(p_comment.m_anchor).trimmed();
  }
  body.replace(QLatin1Char('\n'), QLatin1Char(' '));
  body = truncated(body, c_maxAnchorPreviewChars);
  if (body.isEmpty()) {
    body = anchorPlaceholder(p_comment.m_anchor);
  }
  return prefix + body;
}
} // namespace

CommentPanel::CommentPanel(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setupUI();
}

void CommentPanel::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_list = new QListWidget(this);
  m_list->setAlternatingRowColors(true);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(m_list, 3);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack, 2);

  m_emptyLabel = new QLabel(tr("Select a highlight to edit its note."), m_stack);
  m_emptyLabel->setObjectName(QStringLiteral("CommentEmptyLabel"));
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  // Muted via the shared dynamic property, never an inline stylesheet.
  WidgetUtils::setPropertyDynamically(m_emptyLabel, PropertyDefs::c_mutedText, true);
  m_stack->addWidget(m_emptyLabel);

  auto *editorPage = new QWidget(m_stack);
  auto *editorLayout = new QVBoxLayout(editorPage);
  editorLayout->setContentsMargins(4, 4, 4, 4);

  m_anchorLabel = new QLabel(editorPage);
  m_anchorLabel->setObjectName(QStringLiteral("CommentAnchorLabel"));
  m_anchorLabel->setWordWrap(true);
  // Bound the quote both ways: the text is truncated (see updateEditorForSelection)
  // AND the widget is height-capped, because a wrapped QLabel reports a
  // content-sized minimumSizeHint that would otherwise beat the layout's stretch
  // factors and crowd out the comment list.
  m_anchorLabel->setMaximumHeight(m_anchorLabel->fontMetrics().lineSpacing() *
                                  c_maxAnchorPreviewLines);
  m_anchorLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  m_anchorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  WidgetUtils::setPropertyDynamically(m_anchorLabel, PropertyDefs::c_mutedText, true);
  editorLayout->addWidget(m_anchorLabel);

  m_editor = new QPlainTextEdit(editorPage);
  m_editor->setPlaceholderText(tr("Add a note..."));
  editorLayout->addWidget(m_editor, 1);

  auto *actionsLayout = new QHBoxLayout();
  m_colorBox = new QComboBox(editorPage);
  const auto colors = CommentColor::all();
  for (const auto &token : colors) {
    // Label is the translated display name; the DATA stays the raw token, which
    // is what reaches comments.json. Neither a translation NOR the swatch icon
    // may change what is stored.
    m_colorBox->addItem(swatchIcon(token), CommentColor::displayName(token), token);
  }
  actionsLayout->addWidget(m_colorBox);
  actionsLayout->addStretch();

  m_deleteButton = new QPushButton(tr("Delete"), editorPage);
  actionsLayout->addWidget(m_deleteButton);
  editorLayout->addLayout(actionsLayout);

  m_stack->addWidget(editorPage);
  m_stack->setCurrentWidget(m_emptyLabel);

  m_textTimer = new QTimer(this);
  m_textTimer->setSingleShot(true);
  m_textTimer->setInterval(c_textDebounceMs);

  connect(m_list, &QListWidget::itemSelectionChanged, this, &CommentPanel::onRowActivated);
  connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
    if (m_updating || !m_provider) {
      return;
    }
    // Capture the target NOW. See the m_pendingTextId comment in the header.
    const auto id = m_provider->getSelectedId();
    if (id.isEmpty()) {
      return;
    }
    m_pendingTextId = id;
    m_pendingText = m_editor->toPlainText();
    m_textTimer->start();
  });
  connect(m_textTimer, &QTimer::timeout, this, &CommentPanel::onTextEdited);
  connect(m_colorBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &CommentPanel::onColorPicked);
  connect(m_deleteButton, &QPushButton::clicked, this, &CommentPanel::onDeleteClicked);
}

void CommentPanel::flushPendingTextEdit() {
  m_textTimer->stop();
  if (m_pendingTextId.isEmpty() || !m_provider) {
    m_pendingTextId.clear();
    m_pendingText.clear();
    return;
  }

  const auto id = m_pendingTextId;
  const auto text = m_pendingText;
  m_pendingTextId.clear();
  m_pendingText.clear();
  emit m_provider->textEditRequested(id, text);
}

QIcon CommentPanel::swatchIcon(const QString &p_token) const {
  return CommentColorSwatch::icon(m_resolve, p_token, 16, m_borderCss);
}

void CommentPanel::setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve,
                                     QString p_borderCss) {
  // BOTH are re-supplied, not just the callback: the border travels as a value
  // and would otherwise outlive the theme it came from.
  m_resolve = std::move(p_resolve);
  m_borderCss = std::move(p_borderCss);

  if (!m_colorBox) {
    return;
  }
  // The icons are persistent, so they are rebuilt here rather than lazily. The
  // item DATA is untouched — only the decoration changes.
  for (int i = 0; i < m_colorBox->count(); ++i) {
    m_colorBox->setItemIcon(i, swatchIcon(m_colorBox->itemData(i).toString()));
  }
}

void CommentPanel::setCommentProvider(const QSharedPointer<CommentProvider> &p_provider) {
  if (m_provider == p_provider) {
    return;
  }

  // A pending keystroke belongs to the OLD provider; flush it before switching
  // or it would be applied to a different file's comment.
  flushPendingTextEdit();

  if (m_provider) {
    disconnect(m_provider.data(), nullptr, this, nullptr);
  }

  m_provider = p_provider;

  if (m_provider) {
    connect(m_provider.data(), &CommentProvider::commentsChanged, this,
            &CommentPanel::onCommentsChanged);
    connect(m_provider.data(), &CommentProvider::selectionChanged, this,
            &CommentPanel::onSelectionChanged);
    connect(m_provider.data(), &CommentProvider::editableChanged, this,
            &CommentPanel::onEditableChanged);
  }

  reload();
}

void CommentPanel::onCommentsChanged() { reload(); }

void CommentPanel::onSelectionChanged() {
  if (!m_provider) {
    return;
  }

  // The editor is about to be repainted with a different comment's text, so an
  // edit still in the debounce window has to be committed to its ORIGINAL
  // comment first.
  const auto id = m_provider->getSelectedId();
  if (!m_pendingTextId.isEmpty() && m_pendingTextId != id) {
    flushPendingTextEdit();
    // The flush re-enters through commentsChanged -> reload -> onSelectionChanged
    // with the pending edit already applied, so this pass can proceed normally.
  }

  m_updating = true;
  bool found = false;
  for (int i = 0; i < m_list->count(); ++i) {
    if (m_list->item(i)->data(c_idRole).toString() == id) {
      m_list->setCurrentRow(i);
      found = true;
      break;
    }
  }
  if (!found) {
    m_list->clearSelection();
    m_list->setCurrentRow(-1);
  }
  m_updating = false;

  updateEditorForSelection();
}

void CommentPanel::onEditableChanged() {
  const bool editable = m_provider && m_provider->isEditable();
  m_editor->setReadOnly(!editable);
  m_colorBox->setEnabled(editable);
  m_deleteButton->setEnabled(editable);
}

void CommentPanel::reload() {
  m_updating = true;
  m_list->clear();

  if (m_provider) {
    const auto &comments = m_provider->getComments().m_comments;
    for (const auto &comment : comments) {
      auto *item = new QListWidgetItem(rowLabel(comment), m_list);
      item->setData(c_idRole, comment.m_id);
      if (!comment.hasKnownAnchorType()) {
        // Carried through untouched, but not renderable here.
        item->setToolTip(tr("This comment was created by a newer version of VNote and "
                            "cannot be shown on the page"));
      } else if (!comment.m_text.isEmpty()) {
        item->setToolTip(comment.m_text);
      }
    }
  }
  m_updating = false;

  onEditableChanged();
  onSelectionChanged();
}

QString CommentPanel::selectedId() const {
  auto *item = m_list->currentItem();
  return item ? item->data(c_idRole).toString() : QString();
}

void CommentPanel::updateEditorForSelection() {
  if (!m_provider) {
    m_stack->setCurrentWidget(m_emptyLabel);
    return;
  }

  const auto id = m_provider->getSelectedId();
  const int idx = m_provider->getComments().indexOfId(id);
  if (idx < 0) {
    m_stack->setCurrentWidget(m_emptyLabel);
    return;
  }

  const auto &comment = m_provider->getComments().m_comments[idx];

  m_updating = true;
  // Recomputed per update rather than only at setup: the theme stylesheet can
  // change the label's font after construction, and a cap derived from the
  // default font would then be wrong.
  m_anchorLabel->setMaximumHeight(m_anchorLabel->fontMetrics().lineSpacing() *
                                  c_maxAnchorPreviewLines);

  QString anchorText = PdfQuadsAnchor::text(comment.m_anchor).trimmed();
  // Newlines come straight from the PDF's text layer and are line-break
  // artifacts of the page layout, not sentence structure; collapsing them keeps
  // the quote compact and lets the truncation below be predictable.
  anchorText.replace(QLatin1Char('\n'), QLatin1Char(' '));

  if (anchorText.isEmpty()) {
    // Ink and text boxes have no quote; say what the comment IS instead of
    // reporting an absence.
    m_anchorLabel->setText(anchorPlaceholder(comment.m_anchor));
    m_anchorLabel->setToolTip(QString());
  } else {
    m_anchorLabel->setText(
        QStringLiteral("\u201C%1\u201D").arg(truncated(anchorText, c_maxAnchorPreviewChars)));
    // Nothing is lost: the full selection is one hover away (and is of course
    // still highlighted on the page).
    m_anchorLabel->setToolTip(anchorText);
  }
  if (m_editor->toPlainText() != comment.m_text) {
    m_editor->setPlainText(comment.m_text);
  }
  const int colorIdx = m_colorBox->findData(comment.m_color);
  m_colorBox->setCurrentIndex(colorIdx >= 0 ? colorIdx : 0);
  m_updating = false;

  m_stack->setCurrentIndex(1);
}

void CommentPanel::onRowActivated() {
  if (m_updating || !m_provider) {
    return;
  }
  // Commit whatever was typed into the PREVIOUS comment before the selection
  // (and therefore the editor contents) moves.
  flushPendingTextEdit();
  // Views emit intents; the controller decides.
  emit m_provider->activateRequested(selectedId());
}

void CommentPanel::onTextEdited() { flushPendingTextEdit(); }

void CommentPanel::onColorPicked(int p_index) {
  if (m_updating || !m_provider || p_index < 0) {
    return;
  }
  const auto id = m_provider->getSelectedId();
  if (id.isEmpty()) {
    return;
  }
  // A color change republishes the set, which repaints the editor; flush first
  // or the repaint would restore the last SAVED text over what is being typed.
  flushPendingTextEdit();
  emit m_provider->colorChangeRequested(id, m_colorBox->itemData(p_index).toString());
}

void CommentPanel::onDeleteClicked() {
  if (!m_provider) {
    return;
  }
  const auto id = m_provider->getSelectedId();
  if (id.isEmpty()) {
    return;
  }
  // Drop, do NOT flush: the comment is about to cease to exist, and writing to
  // it first would be a pointless round trip.
  if (m_pendingTextId == id) {
    m_textTimer->stop();
    m_pendingTextId.clear();
    m_pendingText.clear();
  } else {
    flushPendingTextEdit();
  }
  emit m_provider->deleteRequested(id);
}
