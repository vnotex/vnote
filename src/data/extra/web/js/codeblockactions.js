class CodeBlockActions extends VxWorker {
  constructor() {
    super();
    this.name = 'codeblockactions';
    this.collapsedLineCount = 3;
  }

  registerInternal() {
    this.vxcore.on('basicMarkdownRendered', () => {
      try {
        if (window.vxOptions.removeCodeToolBarEnabled) {
          return;
        }

        let toolbars = this.vxcore.contentContainer.querySelectorAll('div.code-toolbar > .toolbar');
        for (let i = 0; i < toolbars.length; ++i) {
          let toolbar = toolbars[i];
          let codeToolbar = toolbar.parentNode;
          let preEl = codeToolbar.querySelector('pre');
          let codeEl = preEl.querySelector('code');
          let lineCount = codeEl.textContent.replace(/\n$/, '').split('\n').length;

          toolbar.innerHTML = '';

          toolbar.appendChild(this.createCopyButton(codeEl));

          if (lineCount > this.collapsedLineCount) {
            toolbar.appendChild(this.createCollapseButton(codeToolbar, preEl, codeEl));
          }

          codeToolbar.style.setProperty('--vx-code-bg', getComputedStyle(preEl).backgroundColor);
        }
      } finally {
        this.finishWork();
      }
    });
  }

  createCopyButton(codeEl) {
    let btn = document.createElement('button');
    btn.className = 'vx-codeblock-action-btn vx-copy-btn';
    btn.title = 'Copy';
    btn.innerHTML =
      '<svg class="vx-icon-copy" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>' +
      '<svg class="vx-icon-check" xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#4caf50" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>';

    btn.addEventListener('click', (e) => {
      e.preventDefault();
      let text = codeEl.textContent;
      this.copyToClipboard(text, btn);
    });

    let wrapper = document.createElement('div');
    wrapper.className = 'toolbar-item';
    wrapper.appendChild(btn);
    return wrapper;
  }

  copyToClipboard(text, btn) {
    let textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.style.cssText = 'position:fixed;left:-9999px;';
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    document.body.removeChild(textarea);
    btn.classList.add('vx-copied');
    setTimeout(() => {
      btn.classList.remove('vx-copied');
    }, 1500);
  }

  createCollapseButton(codeToolbar, preEl, codeEl) {
    let btn = document.createElement('button');
    btn.className = 'vx-codeblock-action-btn vx-collapse-btn';
    btn.title = 'Collapse';
    btn.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>';

    btn.addEventListener('click', () => {
      this.toggleCollapse(codeToolbar, preEl, codeEl, btn);
    });

    let wrapper = document.createElement('div');
    wrapper.className = 'toolbar-item';
    wrapper.appendChild(btn);
    return wrapper;
  }

  toggleCollapse(codeToolbar, preEl, codeEl, btn) {
    if (codeToolbar.classList.contains('vx-collapsed')) {
      codeToolbar.classList.remove('vx-collapsed');
      preEl.style.maxHeight = '';
      btn.title = 'Collapse';
      btn.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>';
    } else {
      let lineHeight = parseFloat(getComputedStyle(codeEl).lineHeight);
      let paddingTop = parseFloat(getComputedStyle(preEl).paddingTop);
      let maxHeight = lineHeight * this.collapsedLineCount + paddingTop * 2;
      preEl.style.maxHeight = maxHeight + 'px';
      codeToolbar.classList.add('vx-collapsed');
      btn.title = 'Expand';
      btn.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"></polyline></svg>';
    }
  }
}

window.vxcore.registerWorker(new CodeBlockActions());
