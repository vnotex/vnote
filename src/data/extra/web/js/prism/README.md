# [clipboard.min.js](https://github.com/zenorocha/clipboard.js)
v2.0.6

# [prism](https://prismjs.com/index.html)
v1.30.0

Built from https://prismjs.com/download with **every** language plus the four
plugins VNote relies on: `line-numbers`, `toolbar`, `copy-to-clipboard` and
`filter-highlight-all`. The exact download URL is preserved in the header
comment of `prism.min.js` — re-download from that URL when upgrading, and keep
the plugin set, since `prism.js` calls `Prism.plugins.filterHighlightAll` and
strips the toolbar at runtime.

The per-theme code colors are NOT taken from Prism's own CSS: every theme ships
its own hand-tuned `themes/<name>/highlight.css`, which is also the source of
the edit-mode token colors (`MarkdownViewWindow2::ensureExternalHighlightStyles`
parses its `.token.*` rules). Do not overwrite those with a stock Prism theme.
