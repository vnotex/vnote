// Self-contained Markdown-link fallback for exported HTML files.
// Local file existence cannot be probed reliably from a file:// page, so this offers the
// corresponding HTML path without claiming that either target exists.
(function () {
  'use strict';

  function markdownHtmlAlternative(href) {
    if (!href || href.charAt(0) === '#' || href.charAt(0) === '/' ||
        href.charAt(0) === '\\' || href.indexOf('//') === 0 ||
        /^[a-z][a-z0-9+.-]*:/i.test(href)) {
      return null;
    }

    var suffixStart = href.search(/[?#]/);
    var path = suffixStart === -1 ? href : href.substring(0, suffixStart);
    if (!/\.md$/i.test(path)) {
      return null;
    }

    var suffix = suffixStart === -1 ? '' : href.substring(suffixStart);
    return path.substring(0, path.length - 3) + '.html' + suffix;
  }

  function bindMarkdownLinkFallback() {
    var root = document.documentElement;
    if (root.getAttribute('data-vx-markdown-link-fallback-bound') === '1') {
      return;
    }
    root.setAttribute('data-vx-markdown-link-fallback-bound', '1');

    document.addEventListener('click', function (event) {
      if (event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.metaKey ||
          event.shiftKey || event.altKey) {
        return;
      }

      var anchor = event.target;
      while (anchor && anchor !== document &&
             (!anchor.tagName || anchor.tagName.toLowerCase() !== 'a')) {
        anchor = anchor.parentNode;
      }
      if (!anchor || anchor === document || anchor.hasAttribute('download')) {
        return;
      }

      var href = anchor.getAttribute('href');
      var alternative = markdownHtmlAlternative(href);
      if (!alternative) {
        return;
      }

      var prompt = 'This link points to a Markdown file. ' +
                   'Open the corresponding HTML file instead?\n\n' + alternative;
      if (!window.confirm(prompt)) {
        return;
      }

      // Let the browser perform the original activation so target and rel keep their semantics.
      // Restore the DOM immediately afterwards for pages opened into another browsing context.
      anchor.setAttribute('href', alternative);
      window.setTimeout(function () {
        anchor.setAttribute('href', href);
      }, 0);
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bindMarkdownLinkFallback);
  } else {
    bindMarkdownLinkFallback();
  }
})();
