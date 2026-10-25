// Classify code nodes by lang.
class CodeNodeStoreByLang {
    constructor() {
        this.prefix = 'lang-';

        // [class] -> NodeList.
        this.knownNodes = new Map();

        // Nodes without lang specified or unknown langs.
        this.unknownNodes = [];
    }

    // Register @p_langs as known langs.
    registerLangs(p_langs) {
        p_langs.forEach((p_lang) => {
            this.knownNodes.set(this.prefix + p_lang, []);
        });
    }

    // Add one node to store.
    addNode(p_node) {
        if (!p_node || p_node.tagName.toLowerCase() != 'code') {
            return;
        }

        for (let i = 0; i < p_node.classList.length; ++i) {
            let key = p_node.classList[i];
            if (key.startsWith(this.prefix)) {
                if (this.knownNodes.has(key)) {
                    let val = this.knownNodes.get(key);
                    val.push(p_node);
                    this.knownNodes.set(key, val);
                    return;
                }
                // We assume that there is only one lang- class.
                break;
            }
        }

        this.unknownNodes.push(p_node);
    }

    // Clear all nodes.
    clearNodes() {
        for (let key of this.knownNodes.keys()) {
            this.knownNodes.set(key, []);
        }

        this.unknownNodes = [];
    }

    getNodes(p_langs) {
        if (!p_langs || p_langs.length == 0) {
            return this.unknownNodes;
        }

        let nodes = [];
        p_langs.forEach((p_lang) => {
            let c = this.prefix + p_lang;
            if (this.knownNodes.has(c)) {
                if (nodes.length == 0) {
                    nodes = this.knownNodes.get(c);
                } else {
                    nodes = nodes.concat(this.knownNodes.get(c));
                }
            }
        });

        return nodes;
    }
}

// Installs native HTML; Markdown syntax and heading identities belong to cmark.
class DocumentRenderer extends VxWorker {
    constructor() {
        super();
        this.name = 'document';
        this.langsToSkipHighlight = new Set();
        this.preNodes = null;
        this.codeNodesStore = new CodeNodeStoreByLang();
        this.codeNodesCollected = false;
        this.xssFilter = null;
    }

    registerInternal() {
        this.vxcore.on('renderedHtmlUpdated', (p_html, p_generation) => {
            this.render(this.vxcore.contentContainer, p_html, p_generation);
        });
    }

    sanitizeFragment(p_html) {
        const urlAttributes = ['href', 'src', 'cite', 'poster'];
        if (!this.xssFilter) {
            const whiteList = Object.assign({}, window.filterXSS.getDefaultWhiteList(), {
                details: ['open'], summary: [], figure: [], figcaption: [],
                input: ['disabled', 'type', 'checked'], span: [], eq: [], eqn: [],
                nav: [], section: [], mark: [], sub: [], sup: []
            });
            // Native decorations and raw HTML share one untrusted fragment. Retain
            // viewer metadata without allowing event attributes or active URLs.
            for (const tag of Object.keys(whiteList)) {
                whiteList[tag] = Array.from(new Set(whiteList[tag].concat([
                    'class', 'id', 'style', 'title', 'role', 'aria-label', 'aria-hidden',
                    'data-source-line', 'data-source-line-end', 'data-sourcepos',
                    'vx-data-anchor-icon'
                ])));
            }
            this.xssFilter = new window.filterXSS.FilterXSS({
                whiteList,
                safeAttrValue: (tag, name, value, cssFilter) => {
                    if (urlAttributes.includes(name)) {
                        // Preserve authored URLs until the inert HTML parser decodes
                        // their attributes. This intermediate HTML is never installed.
                        return window.filterXSS.escapeAttrValue(value);
                    }
                    return window.filterXSS.safeAttrValue(tag, name, value, cssFilter);
                }
            });
        }
        const template = document.createElement('template');
        template.innerHTML = this.xssFilter.process(p_html);
        for (const node of template.content.querySelectorAll('[href], [src], [cite], [poster]')) {
            for (const name of urlAttributes) {
                if (!node.hasAttribute(name)) {
                    continue;
                }
                let url;
                try {
                    // Match browser entity decoding and URL control-character handling,
                    // including java&Tab;script:. Never resolve relative links in place.
                    url = new URL(node.getAttribute(name), document.baseURI);
                } catch (_) {
                    node.removeAttribute(name);
                    continue;
                }
                if (url.protocol === 'javascript:' || url.protocol === 'vbscript:'
                    || (url.protocol === 'data:'
                        && !(node.tagName === 'IMG' && name === 'src'
                            && /^data:image\/(?:gif|png|jpeg|webp|svg\+xml)[;,]/i.test(url.href)))) {
                    node.removeAttribute(name);
                }
            }
        }
        // Connect exactly the nodes checked above; serializing and reparsing could
        // change the interpretation at the security boundary.
        return template.content;
    }

    render(p_node, p_html, p_generation) {
        this.vxcore.emit('markdownContentWillChange', p_node);
        this.codeNodesStore.clearNodes();
        this.codeNodesCollected = false;
        this.preNodes = null;
        if (window.vxOptions.protectedView) {
            this.vxcore.navigationLinkDestinations = new WeakMap();
            return this.renderProtected(p_node, p_html, p_generation);
        }
        if (window.vxOptions.protectFromXss) {
            p_node.replaceChildren(this.sanitizeFragment(p_html));
        } else {
            p_node.innerHTML = p_html;
        }
        this.documentInstalled(p_node, p_generation);
    }

    documentInstalled(p_node, p_generation) {
        if (!this.isCurrentWork(p_generation)) {
            return;
        }
        this.preNodes = p_node.getElementsByTagName('pre');
        // Yield one layout turn before diagrams measure their containing block.
        // No inline event handlers or animation frames: protected CSP and hidden
        // helper/export pages must both complete, including an empty document.
        setTimeout(() => {
            if (!this.isCurrentWork(p_generation)) {
                return;
            }
            p_node.getBoundingClientRect();
            window.vxImageViewer.setupForAllImages(p_node);
            this.vxcore.setBasicMarkdownRendered(p_generation);
            this.finishWork(p_generation);
        }, 0);
    }

    // Parse into inert template contents. Nothing from a note enters the live DOM
    // (including data images) until its element, attributes and bytes are accepted.
    renderProtected(p_node, p_html, p_generation) {
        const template = document.createElement('template');
        template.innerHTML = p_html;
        const tags = new Set(['A', 'ABBR', 'B', 'BLOCKQUOTE', 'BR', 'CAPTION', 'CODE',
            'COL', 'COLGROUP', 'DD', 'DEL', 'DETAILS', 'DIV', 'DL', 'DT', 'EM', 'EQ',
            'EQN', 'FIGCAPTION', 'FIGURE', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6', 'HR',
            'I', 'IMG', 'INPUT', 'KBD', 'LI', 'MARK', 'NAV', 'OL', 'P', 'PRE', 'S', 'SAMP',
            'SECTION', 'SMALL', 'SPAN', 'STRONG', 'SUB', 'SUMMARY', 'SUP', 'TABLE',
            'TBODY', 'TD', 'TH', 'THEAD', 'TR', 'UL']);
        const attrs = new Set(['alt', 'title', 'class', 'id', 'width', 'height',
            'colspan', 'rowspan', 'align', 'start', 'reversed', 'open', 'role',
            'aria-label', 'aria-hidden', 'data-source-line', 'data-source-line-end',
            'vx-data-anchor-icon']);
        const origin = location.protocol + '//' + location.host;
        const pending = [];
        const blockedImage = (image) => {
            const label = document.createElement('span');
            label.className = 'vx-protected-blocked';
            label.textContent = '[Image blocked — use Insert as Base64 to embed it]';
            image.replaceWith(label);
        };
        for (const node of Array.from(template.content.querySelectorAll('*'))) {
            if (node.namespaceURI !== 'http://www.w3.org/1999/xhtml' || !tags.has(node.tagName)) {
                node.remove();
                continue;
            }
            const src = node.getAttribute('src') || '';
            const href = node.getAttribute('href') || '';
            const checkbox = node.tagName === 'INPUT' && node.getAttribute('type') === 'checkbox'
                && node.classList.contains('task-list-item-checkbox');
            const checked = checkbox && node.hasAttribute('checked');
            for (const attr of Array.from(node.attributes)) {
                if (!attrs.has(attr.name) || (attr.name === 'id' && /^vx-/i.test(attr.value))) {
                    node.removeAttribute(attr.name);
                }
            }
            if (node.tagName === 'INPUT') {
                if (!checkbox) {
                    node.remove();
                    continue;
                }
                node.type = 'checkbox';
                node.checked = checked;
            } else if (node.tagName === 'IMG') {
                if (/^data:image\//i.test(src)) {
                    pending.push(new Promise((resolve) => {
                        window.vxMarkdownAdapter.protectedImageUrl(src, (url) => {
                            if (!this.isCurrentWork(p_generation)) {
                                resolve();
                                return;
                            }
                            if (url) {
                                node.src = url;
                            } else {
                                blockedImage(node);
                            }
                            resolve();
                        });
                    }));
                } else if (src && !/^[a-z][a-z0-9+.-]*:/i.test(src)
                           && src[0] !== '/' && !src.includes(String.fromCharCode(92))
                           && !src.includes('?') && !src.includes('#')
                           && Array.from(src).every(ch => ch.charCodeAt(0) >= 32)) {
                    const bytes = new TextEncoder().encode(src);
                    let raw = '';
                    for (const byte of bytes) raw += String.fromCharCode(byte);
                    const token = btoa(raw).split('+').join('-').split('/').join('_').replace(/=+$/, '');
                    node.src = origin + '/files/' + token;
                } else {
                    blockedImage(node);
                }
            } else if (node.tagName === 'A') {
                if (href.startsWith('#')) {
                    this.vxcore.navigationLinkDestinations.set(node, href);
                    node.href = href;
                    node.addEventListener('click', (event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        if (event.isTrusted) {
                            let anchor = href.substring(1);
                            try { anchor = decodeURIComponent(anchor); } catch (_) {}
                            this.vxcore.scrollToAnchor(anchor);
                        }
                    }, true);
                } else if (/^https?:\/\//i.test(href)) {
                    this.vxcore.navigationLinkDestinations.set(node, href);
                    node.href = href;
                    node.rel = 'noreferrer noopener';
                } else if (href && !/^[a-z][a-z0-9+.-]*:/i.test(href)
                           && !/^[\\/]/.test(href) && !/[\u0000-\u001f]/.test(href)) {
                    // Retain the source spelling for notebook navigation, not a
                    // token-prefixed filesystem-looking URL. Never save it back.
                    this.vxcore.navigationLinkDestinations.set(node, href);
                    node.href = '#';
                    node.addEventListener('click', (event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        if (event.isTrusted) {
                            window.vxMarkdownAdapter.activateProtectedLink(href);
                        }
                    }, true);
                }
            }
        }
        return Promise.all(pending).then(() => {
            if (!this.isCurrentWork(p_generation)) {
                return;
            }
            p_node.textContent = '';
            p_node.appendChild(template.content);
            this.documentInstalled(p_node, p_generation);
        });
    }

    static sanitizeProtectedSvg(p_root, p_preserveComputedStyle = true) {
        const tags = new Set(['svg', 'g', 'defs', 'path', 'rect', 'circle', 'ellipse',
            'line', 'polyline', 'polygon', 'text', 'tspan', 'textPath', 'title', 'desc',
            'marker', 'pattern', 'clipPath', 'mask', 'linearGradient', 'radialGradient',
            'stop', 'symbol', 'use']);
        const attrs = new Set(['id', 'class', 'viewBox', 'width', 'height', 'x', 'y',
            'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx', 'ry', 'dx', 'dy', 'd',
            'points', 'transform', 'preserveAspectRatio', 'fill', 'fill-opacity',
            'fill-rule', 'stroke', 'stroke-width', 'stroke-opacity', 'stroke-linecap',
            'stroke-linejoin', 'stroke-dasharray', 'stroke-dashoffset', 'opacity',
            'font-size', 'font-family', 'font-weight', 'font-style', 'text-anchor',
            'dominant-baseline', 'clip-path', 'clip-rule', 'mask', 'marker-start',
            'marker-mid', 'marker-end', 'markerWidth', 'markerHeight', 'markerUnits',
            'orient', 'refX', 'refY', 'gradientUnits', 'gradientTransform', 'offset',
            'stop-color', 'stop-opacity', 'patternUnits', 'patternContentUnits',
            'patternTransform', 'textLength', 'lengthAdjust', 'href', 'xlink:href',
            'xmlns', 'xmlns:xlink']);
        for (const node of [p_root, ...Array.from(p_root.querySelectorAll('*'))]) {
            if (node.namespaceURI !== 'http://www.w3.org/2000/svg' || !tags.has(node.localName)) {
                node.remove();
                continue;
            }
            // Detached native SVG already carries presentation attributes. Only
            // live protected diagrams need computed style copied before stripping CSS.
            if (p_preserveComputedStyle) {
                const style = window.getComputedStyle(node);
                for (const property of ['fill', 'stroke', 'font-size', 'font-family',
                    'font-weight', 'font-style', 'text-anchor', 'stroke-width']) {
                    const value = style.getPropertyValue(property);
                    if (value && !/[\\<>]/.test(value)
                        && (!/url\s*\(/i.test(value) || /^url\(#[\w:.-]+\)$/.test(value))) {
                        node.setAttribute(property, value);
                    }
                }
            }
            for (const attr of Array.from(node.attributes)) {
                const value = attr.value;
                if (!attrs.has(attr.name) || /[\\<>]/.test(value)
                    || ((attr.name === 'href' || attr.name === 'xlink:href')
                        && !/^#[\w:.-]+$/.test(value))
                    || (/url\s*\(/i.test(value) && !/^url\(#[\w:.-]+\)$/.test(value))) {
                    node.removeAttribute(attr.name);
                }
            }
        }
    }

    addLangsToSkipHighlight(p_langs) {
        p_langs.forEach((p_lang) => {
            this.langsToSkipHighlight.add(p_lang);
        });

        this.codeNodesStore.registerLangs(p_langs);
    }

    getCodeNodes(p_langs) {
        if (!this.preNodes) {
            return [];
        }

        if (!this.codeNodesCollected) {
            // Collect code nodes.
            this.codeNodesCollected = true;
            for (let i = 0; i < this.preNodes.length; ++i) {
                this.codeNodesStore.addNode(this.preNodes[i].firstElementChild);
            }
        }

        return this.codeNodesStore.getNodes(p_langs);
    }

}

window.vxcore.registerWorker(new DocumentRenderer());
