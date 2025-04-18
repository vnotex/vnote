#include "themeutils.h"

#include <QApplication>
#include <QPalette>

using namespace vnotex;

QJsonObject ThemeUtils::backfillSystemPalette(QJsonObject p_obj) {
  const auto qpalette = QApplication::palette();

  // Active.
  {
    QJsonObject obj;
    obj["window"] = qpalette.color(QPalette::Active, QPalette::Window).name();
    obj["window_text"] = qpalette.color(QPalette::Active, QPalette::WindowText).name();
    obj["base"] = qpalette.color(QPalette::Active, QPalette::Base).name();
    obj["alternate_base"] = qpalette.color(QPalette::Active, QPalette::AlternateBase).name();
    obj["text"] = qpalette.color(QPalette::Active, QPalette::Text).name();
    obj["button"] = qpalette.color(QPalette::Active, QPalette::Button).name();
    obj["button_text"] = qpalette.color(QPalette::Active, QPalette::ButtonText).name();
    obj["bright_text"] = qpalette.color(QPalette::Active, QPalette::BrightText).name();
    obj["light"] = qpalette.color(QPalette::Active, QPalette::Light).name();
    obj["midlight"] = qpalette.color(QPalette::Active, QPalette::Midlight).name();
    obj["dark"] = qpalette.color(QPalette::Active, QPalette::Dark).name();
    obj["highlight"] = qpalette.color(QPalette::Active, QPalette::Highlight).name();
    obj["highlighted_text"] = qpalette.color(QPalette::Active, QPalette::HighlightedText).name();
    obj["link"] = qpalette.color(QPalette::Active, QPalette::Link).name();
    obj["link_visited"] = qpalette.color(QPalette::Active, QPalette::LinkVisited).name();

    p_obj["active"] = obj;
  }

  // Inactive.
  {
    QJsonObject obj;
    p_obj["inactive"] = obj;
  }

  // Disabled.
  {
    QJsonObject obj;
    obj["window"] = qpalette.color(QPalette::Disabled, QPalette::Window).name();
    obj["window_text"] = qpalette.color(QPalette::Disabled, QPalette::WindowText).name();
    obj["base"] = qpalette.color(QPalette::Disabled, QPalette::Base).name();
    obj["alternate_base"] = qpalette.color(QPalette::Disabled, QPalette::AlternateBase).name();
    obj["text"] = qpalette.color(QPalette::Disabled, QPalette::Text).name();
    obj["button"] = qpalette.color(QPalette::Disabled, QPalette::Button).name();
    obj["button_text"] = qpalette.color(QPalette::Disabled, QPalette::ButtonText).name();
    obj["bright_text"] = qpalette.color(QPalette::Disabled, QPalette::BrightText).name();
    obj["light"] = qpalette.color(QPalette::Disabled, QPalette::Light).name();
    obj["midlight"] = qpalette.color(QPalette::Disabled, QPalette::Midlight).name();
    obj["dark"] = qpalette.color(QPalette::Disabled, QPalette::Dark).name();
    obj["highlight"] = qpalette.color(QPalette::Disabled, QPalette::Highlight).name();
    obj["highlighted_text"] = qpalette.color(QPalette::Disabled, QPalette::HighlightedText).name();
    obj["link"] = qpalette.color(QPalette::Disabled, QPalette::Link).name();
    obj["link_visited"] = qpalette.color(QPalette::Disabled, QPalette::LinkVisited).name();

    p_obj["disabled"] = obj;
  }

  return p_obj;
}
