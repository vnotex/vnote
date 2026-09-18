// Shared code-block toolbar for the live viewer and standalone HTML exports.
(function() {
  'use strict';

  class CodeBlockActions {
    constructor() {
      this.collapsedLineCount = 3;
    }

    bind(container) {
      let toolbars = container.querySelectorAll('div.code-toolbar > .toolbar');
      for (let i = 0; i < toolbars.length; ++i) {
        let toolbar = toolbars[i];
        let codeToolbar = toolbar.parentNode;
        let preEl = codeToolbar.querySelector('pre');
        let codeEl = preEl.querySelector('code');
        let lineCount = codeEl.textContent.replace(/\n$/, '').split('\n').length;

        // Rebuild serialized buttons: DOM attributes survive export, event listeners do not.
        // Repeated initialization replaces the old listeners instead of adding duplicates.
        toolbar.innerHTML = '';
        toolbar.appendChild(this.createCopyButton(codeEl));
        if (lineCount > this.collapsedLineCount) {
          toolbar.appendChild(this.createCollapseButton(codeToolbar, preEl, codeEl));
        } else {
          codeToolbar.classList.remove('vx-collapsed');
          preEl.style.maxHeight = '';
        }
        codeToolbar.style.setProperty('--vx-code-bg', getComputedStyle(preEl).backgroundColor);
      }
    }

    createCopyButton(codeEl) {
      let btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'vx-codeblock-action-btn vx-copy-btn';
      btn.title = window.vxI18n.tr('code.copy');
      btn.setAttribute('aria-label', btn.title);
      btn.innerHTML =
        '<svg class="vx-icon-copy" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>' +
        '<svg class="vx-icon-check" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#4caf50" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>';
      btn.addEventListener('click', (e) => {
        e.preventDefault();
        this.copyToClipboard(codeEl.textContent, btn);
      });
      let wrapper = document.createElement('div');
      wrapper.className = 'toolbar-item';
      wrapper.appendChild(btn);
      return wrapper;
    }

    copyToClipboard(text, btn) {
      // Keep the file://-compatible clipboard path used by read mode.
      let textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.style.cssText = 'position:fixed;left:-9999px;';
      document.body.appendChild(textarea);
      textarea.select();
      try {
        document.execCommand('copy');
      } finally {
        document.body.removeChild(textarea);
      }
      btn.classList.add('vx-copied');
      setTimeout(() => {
        btn.classList.remove('vx-copied');
      }, 1500);
    }

    createCollapseButton(codeToolbar, preEl, codeEl) {
      let btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'vx-codeblock-action-btn vx-collapse-btn';
      this.setCollapsed(codeToolbar, preEl, codeEl, btn,
                        codeToolbar.classList.contains('vx-collapsed'));
      btn.addEventListener('click', () => {
        this.setCollapsed(codeToolbar, preEl, codeEl, btn,
                          !codeToolbar.classList.contains('vx-collapsed'));
      });
      let wrapper = document.createElement('div');
      wrapper.className = 'toolbar-item';
      wrapper.appendChild(btn);
      return wrapper;
    }

    setCollapsed(codeToolbar, preEl, codeEl, btn, collapsed) {
      if (collapsed) {
        let lineHeight = parseFloat(getComputedStyle(codeEl).lineHeight);
        let paddingTop = parseFloat(getComputedStyle(preEl).paddingTop);
        let maxHeight = lineHeight * this.collapsedLineCount + paddingTop * 2;
        preEl.style.maxHeight = maxHeight + 'px';
      } else {
        preEl.style.maxHeight = '';
      }
      codeToolbar.classList.toggle('vx-collapsed', collapsed);
      btn.title = window.vxI18n.tr(collapsed ? 'code.expand' : 'code.collapse');
      btn.setAttribute('aria-label', btn.title);
      btn.setAttribute('aria-expanded', String(!collapsed));
      btn.innerHTML = collapsed
        ? '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"></polyline></svg>'
        : '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>';
    }
  }

  const actions = new CodeBlockActions();
  if (typeof VxWorker !== 'undefined' && window.vxcore) {
    class CodeBlockActionsWorker extends VxWorker {
      constructor() {
        super();
        this.name = 'codeblockactions';
      }

      registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
          try {
            if (!window.vxOptions.removeCodeToolBarEnabled) {
              actions.bind(this.vxcore.contentContainer);
            }
          } finally {
            this.finishWork();
          }
        });
      }
    }
    window.vxcore.registerWorker(new CodeBlockActionsWorker());
  } else {
    const bind = () => actions.bind(document);
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', bind);
    } else {
      bind();
    }
  }
})();
