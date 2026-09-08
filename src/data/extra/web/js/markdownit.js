class MarkdownItOptions {
    constructor() {
        // Enable HTML tags in source.
        this.enableHtmlTag = true;
        // Convert '\n' in paragraphs into <br>.
        this.enableAutoBreaks = false;
        // CSS language prefix for fenced code blocks.
        this.languagePrefix = 'lang-';
        // Convert URL-like text to links.
        this.enableLinkify = true;
        // Enable some language-neural replacement and quotes beautification.
        this.enableTypographer = false;
        // Double and single quotes replacement pairs.
        this.quotes = '';
    }
}

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

class MarkdownIt extends VxWorker {
    constructor(p_options) {
        super();

        this.name = 'markdownit';

        this.options = p_options;
        if (!this.options) {
            this.options = new MarkdownItOptions();
            this.options.enableHtmlTag = window.vxOptions.htmlTagEnabled;
            this.options.enableAutoBreaks = window.vxOptions.autoBreakEnabled;
            this.options.enableLinkify = window.vxOptions.linkifyEnabled;
        }

        // Languages of code blocks that need to skip highlight.
        this.langsToSkipHighlight = new Set();

        // Node to prepend for FrontMatter metadata.
        this.frontMatterNode = null;

        this.lastContainerNode = null;

        // Pre nodes collection.
        this.preNodes = null;

        this.codeNodesStore = new CodeNodeStoreByLang();

        this.codeNodesCollected = false;

        // Used to deduplicate header Ids.
        // One for markdownItAnchor and one for markdownItTocDoneRight.
        this.headerIds = [new Set(), new Set()];

        this.mdit = window.markdownit({
            html: this.options.enableHtmlTag,
            breaks: this.options.enableAutoBreaks,
            linkify: this.options.enableLinkify,
            typographer: this.options.enableTypographer,
            langPrefix: this.options.languagePrefix,
            quotes: this.options.quotes,
            // Defense-in-depth against an unbalanced third-party token pusher inflating
            // state.level (which makes the block tokenizer silently truncate the document).
            // Not the fix for the math-block bug; see markdown-it-texmath.js.
            maxNesting: 500,
            highlight: (p_str, p_lang) => {
                /* We will use asynchronous higlight.
                if (p_lang && !this.langsToSkipHighlight.has(p_lang)) {
                    if (Prism.languages[p_lang]) {
                        return Prism.highlight(p_str, Prism.languages[p_lang], p_lang);
                    }
                }
                */
                // Use external default escaping.
                return '';
            }
        });

        // Enable file: schema of markdownIt.
        this.defaultValidateLink = this.mdit.validateLink;
        this.mdit.validateLink = (p_url) => {
            let str = p_url.trim().toLowerCase();
            // Enable file: schema and SVG data URIs (markdown-it's default only
            // allows gif/png/jpeg/webp data images, blocking data:image/svg+xml).
            if (/^file:/.test(str) || /^data:image\/svg\+xml[;,]/.test(str)) {
                return true;
            }
            return this.defaultValidateLink(p_url);
        };

        this.mdit.use(window.markdownitTaskLists, { enabled: true });

        this.mdit.use(window.markdownitSub);

        this.mdit.use(window.markdownitSup);

        this.mdit.use(window.markdownitEmoji);
        this.mdit.renderer.rules.emoji = function(p_tokens, p_idx) {
            return '<span class="emoji emoji_' + p_tokens[p_idx].markup + '">'
                   + p_tokens[p_idx].content
                   + '</span>';
        };

        this.mdit.use(window.markdownitFootnote);

        this.mdit.use(window['markdown-it-imsize.js']);

        this.mdit.use(texmath, { delimitersList: ['dollars', 'raw'] });

        // Support '::: alert-xxx \n contents \n :::\n'.
        this.mdit.use(window.markdownitContainer, 'alert', {
            validate: function(p_params) {
                return p_params.trim().match(/^alert-\S+$/);
            },

            render: function (p_tokens, p_idx) {
                let type = p_tokens[p_idx].info.trim().match(/^(alert-\S+)$/);
                if (p_tokens[p_idx].nesting === 1) {
                    // opening tag
                    let alertClass = type[1];
                    return '<div class="vx-alert ' + alertClass + '" role="alert">';
                } else {
                    // closing tag
                    return '</div>\n';
                }
            }
        });

        this.mdit.use(window.markdownitFrontMatter, (p_metaData) => {
            if (p_metaData) {
                let detailsNode = document.createElement('details');
                detailsNode.classList.add('vx-frontmatter');

                let summaryNode = document.createElement('summary');
                summaryNode.textContent = 'Metadata';
                detailsNode.appendChild(summaryNode);

                let preNode = document.createElement('pre');
                preNode.textContent = p_metaData;
                detailsNode.appendChild(preNode);

                this.frontMatterNode = detailsNode;
            } else {
                this.frontMatterNode = null;
            }
        });

        this.mdit.use(window.markdownitInjectLinenumbers);

        if (window.vxOptions.protectFromXss && !window.vxOptions.protectedView) {
            let scriptFolderPath = Utils.parentFolder(document.currentScript.src);
            Utils.loadScripts([scriptFolderPath + '/markdown-it/xss.min.js',
                               scriptFolderPath + '/markdown-it/markdown-it-xss.js'],
                              () => {
                                  this.mdit.use(window.markdownItXSS, {
                                      whiteList: {
                                          input: ["style", "class", "disabled", "type", "checked"],
                                          span: ["style", "class"],
                                      }
                                  });
                              });
        }

        this.mdit.use(window.markdownItAnchor, {
            slugify: (str) => {
                return this.generateHeaderId(this.headerIds[0], str);
            },
            permalink: true,
            permalinkBefore: false,
            permalinkClass: 'vx-header-anchor',
            permalinkSpace: false,
            // We use CSS:after to add the mark.
            permalinkSymbol: '',
            permalinkAttrs: (slug, state) => {
                return {
                    'vx-data-anchor-icon': '¶'
                }
            }
        });

        this.mdit.use(window.markdownItTocDoneRight, {
            slugify: (str) => {
                return this.generateHeaderId(this.headerIds[1], str);
            },
            containerClass: 'vx-table-of-contents'
        });

        this.mdit.use(window.markdownitImplicitFigure, {
            figcaption: true
        });

        this.mdit.use(window.markdownitMark);
    }

    registerInternal() {
        this.vxcore.on('markdownTextUpdated', (p_text) => {
            this.render(this.vxcore.contentContainer,
                        p_text,
                        'window.vxcore.getWorker(\'markdownit\').markdownRenderFinished();');
        });
    }

    // Render Markdown @p_text to HTML in @p_node.
    // @p_finishCbStr will be called after finishing loading new content nodes.
    // This could prevent Mermaid Gantt from negative width error.
    render(p_node, p_text, p_finishCbStr) {
        this.frontMatterNode = null;
        this.codeNodesStore.clearNodes();
        this.codeNodesCollected = false;
        this.headerIds[0].clear();
        this.headerIds[1].clear();

        if (p_node != this.lastContainerNode) {
            this.lastContainerNode = p_node;
            this.preNodes = null;
        }

        if (!p_text) {
            p_node.innerHTML = '';
            this.finishWork();
            this.markdownRenderFinished();
            return;
        }

        let html = this.mdit.render(p_text);
        if (window.vxOptions.protectedView) {
            this.renderProtected(p_node, html);
            return;
        }
        p_node.innerHTML = html + this.loadedGuard(p_finishCbStr);

        if (this.preNodes == null) {
            this.preNodes = p_node.getElementsByTagName('pre');
        }

        if (this.frontMatterNode) {
            p_node.insertAdjacentElement('afterbegin', this.frontMatterNode);
        }

        this.finishWork();
    }

    // Parse into inert template contents. Nothing from a note enters the live DOM
    // (including data images) until its element, attributes and bytes are accepted.
    renderProtected(p_node, p_html) {
        const template = document.createElement('template');
        template.innerHTML = p_html;
        const tags = new Set(['A', 'ABBR', 'B', 'BLOCKQUOTE', 'BR', 'CAPTION', 'CODE',
            'COL', 'COLGROUP', 'DD', 'DEL', 'DETAILS', 'DIV', 'DL', 'DT', 'EM', 'EQ',
            'EQN', 'FIGCAPTION', 'FIGURE', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6', 'HR',
            'I', 'IMG', 'INPUT', 'KBD', 'LI', 'MARK', 'OL', 'P', 'PRE', 'S', 'SAMP',
            'SECTION', 'SMALL', 'SPAN', 'STRONG', 'SUB', 'SUMMARY', 'SUP', 'TABLE',
            'TBODY', 'TD', 'TH', 'THEAD', 'TR', 'UL']);
        const attrs = new Set(['alt', 'title', 'class', 'id', 'width', 'height',
            'colspan', 'rowspan', 'align', 'start', 'reversed', 'open', 'role',
            'aria-label', 'aria-hidden', 'data-source-line', 'data-source-line-end',
            'vx-data-anchor-icon']);
        const asset = /^vxasset:([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$/;
        const origin = location.protocol + '//' + location.host;
        const pending = [];
        const blockedImage = (image) => {
            const label = document.createElement('span');
            label.className = 'vx-protected-blocked';
            label.textContent = '[Image blocked in protected note — import it to view]';
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
                const match = asset.exec(src);
                if (match) {
                    node.src = origin + '/assets/' + match[1];
                } else if (/^data:image\//i.test(src)) {
                    pending.push(new Promise((resolve) => {
                        window.vxMarkdownAdapter.protectedImageUrl(src, (url) => {
                            if (url) {
                                node.src = url;
                            } else {
                                blockedImage(node);
                            }
                            resolve();
                        });
                    }));
                } else {
                    blockedImage(node);
                }
            } else if (node.tagName === 'A') {
                const match = asset.exec(href);
                if (match) {
                    node.href = origin + '/assets/' + match[1];
                } else if (href.startsWith('#')) {
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
                    node.href = href;
                    node.rel = 'noreferrer noopener';
                } else if (href && !/^[a-z][a-z0-9+.-]*:/i.test(href)
                           && !/^[\\/]/.test(href) && !/[\u0000-\u001f]/.test(href)) {
                    // Retain the source spelling for notebook navigation, not a
                    // token-prefixed filesystem-looking URL. Never save it back.
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
        Promise.all(pending).then(() => {
            p_node.textContent = '';
            p_node.appendChild(template.content);
            this.preNodes = p_node.getElementsByTagName('pre');
            if (this.frontMatterNode) {
                p_node.insertAdjacentElement('afterbegin', this.frontMatterNode);
            }
            this.finishWork();
            // CSP intentionally disallows the ordinary inline onload guard.
            // Yield a layout turn without compiling an event-handler string.
            setTimeout(() => this.markdownRenderFinished(), 0);
        });
    }

    static sanitizeProtectedSvg(p_root) {
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
            // Preserve computed passive presentation before removing CSS. No
            // stylesheet or CSS escape remains capable of introducing a URL.
            const style = window.getComputedStyle(node);
            for (const property of ['fill', 'stroke', 'font-size', 'font-family',
                'font-weight', 'font-style', 'text-anchor', 'stroke-width']) {
                const value = style.getPropertyValue(property);
                if (value && !/[\\<>]/.test(value)
                    && (!/url\s*\(/i.test(value) || /^url\(#[\w:.-]+\)$/.test(value))) {
                    node.setAttribute(property, value);
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

    loadedGuard(p_cbStr) {
        if (!p_cbStr) {
            return '';
        }
        // Add 1x1 transparent GIF image at the end to monitor the load process.
        return '<img src="data:image/gif;base64,R0lGODlhAQABAIAAAP///wAAACH5BAEAAAAALAAAAAABAAEAAAICRAEAOw==" onload="'
               + p_cbStr + ' try { this.parentNode.removeChild(this); } catch(error) { console.log(error); }">';
    }

    addLangsToSkipHighlight(p_langs) {
        p_langs.forEach((p_lang) => {
            this.langsToSkipHighlight.add(p_lang);
        });

        this.codeNodesStore.registerLangs(p_langs);
    }

    // Will be called when basic markdown is rendered.
    markdownRenderFinished() {
        window.vxImageViewer.setupForAllImages(this.lastContainerNode);
        this.vxcore.setBasicMarkdownRendered();
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

    // Resolve the anchor id of the heading starting at 0-based source line
    // @p_lineNumber in @p_text. Parses the whole document with the live
    // markdown-it instance so the id matches the render path exactly, including
    // reference-style links, footnotes and duplicate-heading numbering.
    getHeadingAnchor(p_text, p_lineNumber) {
        // Swap in fresh dedup state so we never disturb the live render.
        const savedHeaderIds = this.headerIds;
        const savedFrontMatterNode = this.frontMatterNode;
        this.headerIds = [new Set(), new Set()];

        try {
            let tokens = this.mdit.parse(p_text, {});
            for (let i = 0; i < tokens.length; ++i) {
                let token = tokens[i];
                if (token.type === 'heading_open'
                    && token.map
                    && token.map[0] === p_lineNumber) {
                    return { found: true, anchor: token.attrGet('id') || '' };
                }
            }
        } finally {
            this.headerIds = savedHeaderIds;
            this.frontMatterNode = savedFrontMatterNode;
        }

        return { found: false, anchor: '' };
    }

    generateHeaderId(p_headerIds, p_str) {
        // Step 1: Strip VNote heading sequence numbers.
        let regExp = Utils.headingSequenceRegExp();
        let text = p_str.replace(regExp, '');

        // Step 2: Unicode-aware lowercase.
        text = text.toLowerCase();

        // Step 3: Remove characters NOT in keep-set.
        // Keep: Letters (\p{L}), Marks (\p{M}), Numbers (\p{N}),
        //       Connector Punctuation (\p{Pc} — includes _),
        //       Hyphen-minus (U+002D), Space (U+0020).
        text = text.replace(/[^\p{L}\p{M}\p{N}\p{Pc}\u002D\u0020]/gu, '');

        // Step 4: Replace spaces with hyphens.
        text = text.replace(/ /g, '-');

        // Step 5: Deduplicate.
        let id = text;
        let idx = 1;
        while (p_headerIds.has(id)) {
            id = text + '-' + idx;
            ++idx;
        }
        p_headerIds.add(id);
        return id;
    }
}

window.vxcore.registerWorker(new MarkdownIt(null));
