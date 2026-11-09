#include "simplemarkdowneditor.h"

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <utils/widgetutils.h>

using namespace vnotex;

namespace {
QSharedPointer<vte::MarkdownEditorConfig> createConfig(ServiceLocator &p_services) {
  auto *themeService = p_services.get<ThemeService>();
  Q_ASSERT(themeService);
  auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
  textConfig->m_theme = vte::Theme::createThemeFromContent(themeService->fetchTextEditorStyle());
  textConfig->m_syntaxTheme = themeService->getEditorHighlightTheme();
  textConfig->m_scaleFactor = WidgetUtils::calculateScaleFactor();
  textConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::None;
  textConfig->m_textFoldingEnabled = false;

  auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
  config->m_inplacePreviewSources = vte::MarkdownEditorConfig::NoInplacePreview;
  config->m_concealRules.setPreset(vte::MarkdownConcealPreset::Aggressive);
  return config;
}
} // namespace

SimpleMarkdownEditor::SimpleMarkdownEditor(ServiceLocator &p_services, QWidget *p_parent)
    : vte::VMarkdownEditor(createConfig(p_services),
                           QSharedPointer<vte::TextEditorParameters>::create(), p_parent),
      m_services(p_services) {
  enableInternalContextMenu();
  setInplacePreviewEnabled(false);
  connect(getTextEdit(), &QTextEdit::textChanged, this, &SimpleMarkdownEditor::textChanged);
  connect(m_services.get<ThemeService>(), &ThemeService::themeChanged, this,
          [this]() { setConfig(createConfig(m_services)); });
}

QString SimpleMarkdownEditor::toPlainText() const { return getTextEdit()->toPlainText(); }

void SimpleMarkdownEditor::setPlainText(const QString &p_text) { setText(p_text); }
