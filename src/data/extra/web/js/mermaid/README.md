# [mermaid](https://github.com/mermaid-js/mermaid)
v11.16.0

## Local patch

Backport of [PR #8233](https://github.com/mermaid-js/mermaid/pull/8233) for
[issue #7794](https://github.com/mermaid-js/mermaid/issues/7794): `addHtmlSpan` wraps HTML
labels when the measured width is `>= wrappingWidth - 1`, rather than exactly equal to
`wrappingWidth`. The one-CSS-pixel tolerance prevents clipped labels at fractional display scaling.

Preserve this patch when refreshing `mermaid.min.js` until upstream includes an equivalent fix.
