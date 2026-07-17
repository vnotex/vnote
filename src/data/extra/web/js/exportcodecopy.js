// Self-contained copy-button handler for exported HTML files.
// Dependency-free (no vxcore / VxWorker): the exported file is a static page, so the copy
// logic that lives in the live viewer runtime (codeblockactions.js) is not available. This
// script re-binds the click handler on the serialized copy buttons. It mirrors the textarea
// + document.execCommand('copy') fallback so it works on the file:// origin, where the async
// Clipboard API may be blocked.
(function () {
  'use strict';

  function copyToClipboard(text, btn) {
    var textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.style.cssText = 'position:fixed;left:-9999px;';
    document.body.appendChild(textarea);
    textarea.select();
    try {
      document.execCommand('copy');
    } catch (e) {
      // Ignore; nothing more we can do in a static file.
    }
    document.body.removeChild(textarea);
    btn.classList.add('vx-copied');
    setTimeout(function () {
      btn.classList.remove('vx-copied');
    }, 1500);
  }

  function bindCopyButtons() {
    var buttons = document.querySelectorAll('.vx-copy-btn');
    for (var i = 0; i < buttons.length; ++i) {
      (function (btn) {
        // Idempotence guard: never bind twice, even if this script is injected more than once
        // (e.g. a persisted exportResource still references exportcodecopy.js).
        if (btn.getAttribute('data-vx-copy-bound') === '1') {
          return;
        }
        var codeToolbar = btn.closest ? btn.closest('div.code-toolbar') : null;
        if (!codeToolbar) {
          var node = btn.parentNode;
          while (node && !(node.classList && node.classList.contains('code-toolbar'))) {
            node = node.parentNode;
          }
          codeToolbar = node;
        }
        if (!codeToolbar) {
          return;
        }
        var codeEl = codeToolbar.querySelector('pre code');
        if (!codeEl) {
          return;
        }
        btn.setAttribute('data-vx-copy-bound', '1');
        btn.addEventListener('click', function (e) {
          e.preventDefault();
          copyToClipboard(codeEl.textContent, btn);
        });
      })(buttons[i]);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bindCopyButtons);
  } else {
    bindCopyButtons();
  }
})();
