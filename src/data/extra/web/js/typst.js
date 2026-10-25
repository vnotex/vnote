// This mandatory worker may also appear in a user-configured script list.
(() => {
    if (window.vxcore.getWorker('typst')) {
        return;
    }

    class TypstRenderer extends VxWorker {
        constructor() {
            super();
            this.name = 'typst';
            this.langs = ['typst'];
            this.requestId = 0;
            this.requests = new Map();
            this.readerScope = null;
        }

        registerInternal() {
            this.vxcore.getWorker('document').addLangsToSkipHighlight(this.langs);
            this.vxcore.on('markdownContentWillChange', node => {
                if (node === this.vxcore.contentContainer && this.readerScope) {
                    const scope = this.readerScope;
                    this.readerScope = null;
                    this.cancelScope(scope);
                }
            });
            this.vxcore.on('basicMarkdownRendered', () => this.render());
        }

        initializeNative() {
            return Promise.resolve().then(() => {
                if (!window.vxTypstBridge) {
                    throw new Error('Native Typst bridge unavailable');
                }
            });
        }

        requestMath(p_scope, p_source, p_display, p_container, p_reading = false) {
            return this.request('math', p_scope, p_source, p_display, p_container, p_reading);
        }

        requestMarkup(p_scope, p_source, p_container, p_reading = false) {
            return this.request('markup', p_scope, p_source, false, p_container, p_reading);
        }

        // Register ownership before initialization yields, including in hidden hosts.
        request(p_kind, p_scope, p_source, p_display, p_container, p_reading) {
            const id = 'typst-' + this.id + '-' + (++this.requestId);
            return new Promise((resolve, reject) => {
                const request = { kind: p_kind, scope: p_scope, source: p_source,
                    reading: p_reading, resolve, reject };
                this.requests.set(id, request);
                this.initializeNative().then(() => {
                    if (this.requests.get(id) !== request) {
                        return;
                    }
                    // Offscreen helper geometry must not determine intrinsic sizing.
                    const style = window.getComputedStyle(p_reading ? p_container : this.vxcore.contentContainer);
                    request.fontSize = parseFloat(style.fontSize);
                    if (!Number.isFinite(request.fontSize) || request.fontSize <= 0) {
                        throw new Error('Invalid Typst font size');
                    }
                    const foreground = TypstRenderer.foreground(style.color);
                    if (p_kind === 'math') {
                        window.vxTypstBridge.renderTypstMath(id, p_scope, p_source, p_display, foreground);
                    } else {
                        window.vxTypstBridge.renderTypstBlock(id, p_scope, p_source, foreground);
                    }
                }).catch(error => {
                    if (this.requests.delete(id)) {
                        reject(error);
                    }
                });
            });
        }

        handleNativeResult(p_id, p_result) {
            const request = this.requests.get(p_id);
            if (!request) {
                return;
            }
            this.requests.delete(p_id);
            try {
                if (!p_result || typeof p_result.error !== 'string') {
                    throw new Error('Invalid Typst reply');
                }
                if (p_result.error) {
                    throw new Error(p_result.error);
                }
                request.resolve(this.createSvg(p_result, p_id, request.source,
                    request.fontSize, request.reading, request.kind));
            } catch (error) {
                request.reject(error);
            }
        }

        cancelScope(p_scope) {
            for (const [id, request] of this.requests) {
                if (request.scope === p_scope) {
                    this.requests.delete(id);
                    request.reject(new Error('Typst rendering was cancelled'));
                }
            }
            // Clear JS ownership before a bridge callback can settle an old ID.
            if (window.vxTypstBridge) {
                window.vxTypstBridge.cancelTypst(p_scope);
            }
        }

        static foreground(p_color) {
            // Preserve sRGB channels at fractional alpha without premultiplication loss.
            const match = /^rgba?\(([^)]+)\)$/.exec(p_color);
            let bytes;
            if (match) {
                const channels = match[1].trim().split(/[\s,/]+/).map(Number);
                if ((channels.length !== 3 && channels.length !== 4)
                    || !channels.every(Number.isFinite)) {
                    throw new Error('Invalid Typst foreground');
                }
                bytes = channels.slice(0, 3);
                bytes.push((channels.length === 4 ? channels[3] : 1) * 255);
            } else {
                const canvas = document.createElement('canvas');
                canvas.width = canvas.height = 1;
                const context = canvas.getContext('2d');
                context.fillStyle = p_color;
                context.fillRect(0, 0, 1, 1);
                bytes = Array.from(context.getImageData(0, 0, 1, 1).data);
            }
            return '#' + bytes.map(value => Math.round(Math.max(0, Math.min(255, value)))
                .toString(16).padStart(2, '0')).join('').toUpperCase();
        }

        createSvg(p_result, p_id, p_source, p_fontSize, p_reading, p_kind) {
            const { widthPt, heightPt, baselinePt, pageCount, svg } = p_result;
            const markup = p_kind === 'markup';
            if (![widthPt, heightPt, baselinePt].every(Number.isFinite)
                || widthPt <= 0 || heightPt <= 0 || widthPt > 16384 || heightPt > 16384
                || !Number.isSafeInteger(pageCount) || pageCount <= 0
                || (markup ? baselinePt !== 0 : pageCount !== 1)
                || typeof svg !== 'string' || !svg.length || svg.length > 8 * 1024 * 1024
                || new TextEncoder().encode(svg).length > 8 * 1024 * 1024) {
                throw new Error('Invalid Typst SVG geometry or size');
            }
            const xml = new DOMParser().parseFromString(svg, 'image/svg+xml');
            const root = xml.documentElement;
            const svgNamespace = 'http://www.w3.org/2000/svg';
            if (xml.doctype || xml.querySelector('parsererror') || !root
                || root.namespaceURI !== svgNamespace || root.localName !== 'svg') {
                throw new Error('Invalid Typst SVG document');
            }
            const viewBox = (root.getAttribute('viewBox') || '').trim().split(/[\s,]+/).map(Number);
            if (viewBox.length !== 4 || !viewBox.every(Number.isFinite)
                || viewBox[2] <= 0 || viewBox[3] <= 0
                || Math.abs(viewBox[2] - widthPt) > 0.02 || Math.abs(viewBox[3] - heightPt) > 0.02) {
                throw new Error('Invalid Typst SVG viewport');
            }
            for (const node of [root, ...Array.from(root.querySelectorAll('*'))]) {
                // Color glyphs can emit images even without a native FrameItem::Image.
                // Failing the whole fragment is safer than silently losing its content.
                if (node.localName === 'image' || node.localName === 'foreignObject') {
                    throw new Error('Typst SVG images are unsupported');
                }
                // Native link annotations contain only transparent hit rectangles; the
                // sanitizer removes them, while independently painted text survives.
                if (!node.closest('a')) {
                    for (const attr of Array.from(node.attributes)) {
                        if (attr.namespaceURI === 'http://www.w3.org/2000/xmlns/') {
                            continue;
                        }
                        if (attr.localName === 'href' && !/^#[\w:.-]+$/.test(attr.value)) {
                            throw new Error('External Typst SVG resource');
                        }
                        for (const match of attr.value.matchAll(/url\(\s*([^)]*?)\s*\)/gi)) {
                            if (!/^#[\w:.-]+$/.test(match[1])) {
                                throw new Error('External Typst SVG resource');
                            }
                        }
                    }
                }
                if (node.namespaceURI === svgNamespace && node.localName === 'symbol') {
                    if (!node.id || node.getAttribute('overflow') !== 'visible'
                        || ['viewBox', 'width', 'height'].some(attr => node.hasAttribute(attr))) {
                        throw new Error('Unsupported Typst glyph viewport');
                    }
                    const group = xml.createElementNS(svgNamespace, 'g');
                    for (const attr of Array.from(node.attributes)) {
                        group.setAttributeNS(attr.namespaceURI, attr.name, attr.value);
                    }
                    while (node.firstChild) {
                        group.appendChild(node.firstChild);
                    }
                    node.parentNode.replaceChild(group, node);
                }
            }
            if (window.vxOptions.protectedView) {
                // Qt accepts RGB paints plus independent channel opacity, not RGBA hex.
                for (const node of [root, ...Array.from(root.querySelectorAll('*'))]) {
                    for (const [paint, opacity] of [['fill', 'fill-opacity'],
                        ['stroke', 'stroke-opacity'], ['stop-color', 'stop-opacity']]) {
                        const match = /^#([\da-f]{6})([\da-f]{2})$/i.exec(node.getAttribute(paint) || '');
                        if (!match) {
                            continue;
                        }
                        const value = (node.getAttribute(opacity) || '1').trim();
                        const alpha = value.endsWith('%') ? Number(value.slice(0, -1)) / 100 : Number(value);
                        if (!Number.isFinite(alpha)) {
                            throw new Error('Invalid Typst paint opacity');
                        }
                        node.setAttribute(paint, '#' + match[1]);
                        node.setAttribute(opacity, parseInt(match[2], 16) / 255
                            * Math.max(0, Math.min(1, alpha)));
                    }
                }
            }
            // No unchecked native SVG is attached to a live node to obtain style.
            DocumentRenderer.sanitizeProtectedSvg(root, false);
            const nodes = [root, ...Array.from(root.querySelectorAll('*'))];
            const ids = new Map();
            for (const node of nodes) {
                if (node.hasAttribute('id')) {
                    const oldId = node.getAttribute('id');
                    if (ids.has(oldId)) {
                        throw new Error('Duplicate Typst SVG identifier');
                    }
                    const newId = node === root ? p_id : p_id + '-' + oldId;
                    ids.set(oldId, newId);
                    node.setAttribute('id', newId);
                }
            }
            const localReference = id => {
                if (!ids.has(id)) {
                    throw new Error('Unresolved Typst SVG reference');
                }
                return '#' + ids.get(id);
            };
            for (const node of nodes) {
                for (const attr of Array.from(node.attributes)) {
                    let value = attr.value;
                    if (attr.name === 'href' || attr.name === 'xlink:href') {
                        value = localReference(value.substring(1));
                    } else {
                        value = value.replace(/url\(#([\w:.-]+)\)/g,
                            (match, id) => 'url(' + localReference(id) + ')');
                    }
                    if (value !== attr.value) {
                        node.setAttributeNS(attr.namespaceURI, attr.name, value);
                    }
                }
            }
            root.setAttribute('id', p_id);
            const result = document.importNode(root, true);
            result.setAttribute('class', markup ? 'typst-block' : 'typst-math');
            result.setAttribute('data-typst-pages', String(pageCount));
            const label = markup ? 'Typst' : p_source;
            result.setAttribute('aria-label', label);
            const title = document.createElementNS(svgNamespace, 'title');
            title.textContent = label;
            result.insertBefore(title, result.firstChild);
            const scale = (p_reading ? 1 : p_fontSize) / 12;
            const unit = p_reading ? 'em' : '';
            result.setAttribute('width', widthPt * scale + unit);
            result.setAttribute('height', heightPt * scale + unit);
            result.style.display = markup ? 'block' : 'inline-block';
            if (!markup) {
                result.style.verticalAlign = (baselinePt - heightPt) * scale + (p_reading ? 'em' : 'px');
            }
            return result;
        }

        isCurrentReader(p_generation, p_scope) {
            return this.isCurrentWork(p_generation) && this.readerScope === p_scope
                && !this.vxcore.pendingData.document;
        }

        async render() {
            const generation = this.workGeneration;
            const scope = 'reader:' + this.vxcore.documentGeneration;
            this.readerScope = scope;
            try {
                const nodes = this.vxcore.getWorker('document').getCodeNodes(this.langs);
                for (const node of nodes) {
                    if (!this.isCurrentReader(generation, scope)) {
                        return;
                    }
                    const original = node.parentElement.localName === 'pre' ? node.parentElement : node;
                    try {
                        // Use the surrounding prose font, not the code block's monospace font.
                        const svg = await this.requestMarkup(scope, node.textContent, original.parentElement, true);
                        if (!this.isCurrentReader(generation, scope)) {
                            return;
                        }
                        const section = document.createElement('section');
                        section.classList.add('typst-block-container');
                        Utils.checkSourceLine(original, section);
                        Utils.checkSourceLine(node, section);
                        for (const attr of ['data-source-line', 'data-source-line-end', 'data-sourcepos']) {
                            const value = original.getAttribute(attr) || node.getAttribute(attr);
                            if (value !== null) {
                                section.setAttribute(attr, value);
                            }
                        }
                        section.appendChild(svg);
                        Utils.replaceNodeWithPreCheck(node, section);
                    } catch (error) {
                        if (!this.isCurrentReader(generation, scope)) {
                            return;
                        }
                        // Retain exact code and source mapping. Never expose source as a label.
                        node.setAttribute('title', error.message || String(error));
                    }
                }
            } finally {
                this.finishWork(generation);
            }
        }

        prepareForExport(p_options) {
            if (!p_options || (!p_options.rasterizeMath && !p_options.rasterizeDiagrams)) {
                return null;
            }
            const roots = this.vxcore.contentContainer.querySelectorAll('svg.typst-block');
            return Promise.all(Array.from(roots, async svg => {
                try {
                    const bounds = svg.getBoundingClientRect();
                    const width = Math.max(1, Math.ceil(bounds.width));
                    const height = Math.max(1, Math.ceil(bounds.height));
                    const clone = svg.cloneNode(true);
                    clone.setAttribute('width', width + 'px');
                    clone.setAttribute('height', height + 'px');
                    const serialized = new XMLSerializer().serializeToString(clone);
                    // SvgToImage owns/revokes its blob URL on either image completion.
                    const raster = await new Promise((resolve, reject) => {
                        SvgToImage.svgToImage(serialized, { crossOrigin: 'Anonymous' }, (error, image) => {
                            if (error) { reject(error); } else { resolve(image); }
                        });
                    });
                    const scale = Math.max(2, window.devicePixelRatio || 1);
                    const canvas = document.createElement('canvas');
                    canvas.width = Math.ceil(width * scale);
                    canvas.height = Math.ceil(height * scale);
                    canvas.getContext('2d').drawImage(raster, 0, 0, canvas.width, canvas.height);
                    const image = document.createElement('img');
                    image.src = canvas.toDataURL('image/png');
                    image.setAttribute('data-typst-block-png', 'true');
                    image.setAttribute('alt', 'Typst');
                    image.style.width = width + 'px';
                    image.style.height = height + 'px';
                    image.style.display = 'block';
                    if (svg.parentNode) {
                        svg.parentNode.replaceChild(image, svg);
                    }
                } catch (error) {
                    console.error('failed to rasterize Typst block');
                }
            }));
        }
    }

    window.vxcore.registerWorker(new TypstRenderer());
})();
