# LaTeX Dark

A dark counterpart to LaTeX Light for VNote: serif body text, a capped measure, weight-led headings and
booktabs-style tables, so a note reads like a typeset article rather than a web page.

## Inspiration and attribution

The *visual language* of this theme is inspired by
[typora-latex-theme](https://github.com/Keldos-Li/typora-latex-theme).

Nothing is ported from it. That project is licensed GPL-3.0 while VNote is LGPLv3, so no CSS,
SCSS, selectors, comments, generated CSS, assets or distinctive rule combinations were copied or
adapted from it. Every file in this folder is authored natively against VNote's own palette-token
model and its QtWebEngine viewer DOM. No upstream license text is inherited and none applies.

## Fonts

No fonts are bundled. The stylesheets ask for **Latin Modern Roman** / **Latin Modern Mono** first
and fall back through Times / Georgia / `serif` and Consolas / `monospace`, so the theme is usable
out of the box. Installing [Latin Modern](https://www.gust.org.pl/projects/e-foundry/latin-modern)
— and, for Chinese text, Noto Serif CJK or Source Han Serif — gives the intended look.

## What is deliberately not here

- **No section numbering.** VNote numbers headings in the *outline* only, behind the
  `outlineSectionNumberEnabled` preference. CSS counters here would number the viewer
  unconditionally and out of step with that preference.
- **No print page geometry, cover page or abstract.** VNote's viewer is a screen; export uses the
  same stylesheet.
- **No justified text.** WebEngine does not hyphenate, so justification produces rivers.
