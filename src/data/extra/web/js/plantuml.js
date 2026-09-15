class PlantUml extends GraphRenderer {
    constructor() {
        super();

        this.name = 'plantuml';

        this.graphDivClass = 'vx-plantuml-graph';

        this.extraScripts = [this.scriptFolderPath + '/plantuml/synchro2.js',
                             this.scriptFolderPath + '/plantuml/zopfli.raw.min.js'];

        this.serverUrl = 'http://www.plantuml.com/plantuml';

        this.format = 'svg';

        this.langs = ['plantuml', 'puml'];

        this.useWeb = true;

        this.nextLocalGraphIndex = 1;
        this.nextSvgIndex = 1;

        // (serverUrl, format, text) -> encoded URL. getPlantUMLOnlineUrl() runs
        // Zopfli synchronously, which is what makes the dispatch loop cost 2.6 s for
        // 200 diagrams; a document that repeats a diagram pays it once.
        this.urlCache = new LruCache(256);
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(
                window.vxOptions.transformSvgToPngEnabled
                    || window.vxOptions.plantUmlFormat === 'png' ? 'png' : 'svg');
        });

        this.vxcore.getWorker('markdownit').addLangsToSkipHighlight(this.langs);

        this.useWeb = window.vxOptions.webPlantUml;
        if (!this.useWeb) {
            this.extraScripts = [];
        }

        // Web mode is I/O-bound once the request is out, but building each URL
        // costs a synchronous Zopfli compression, so an unbounded dispatch loop
        // blocks the page for the whole document. A large batch keeps plenty of
        // requests in flight while still yielding to the compositor. The local path
        // is a process round trip with no such synchronous cost, so leave it
        // unbounded. See GraphRenderer.concurrencyLimit.
        this.concurrencyLimit = this.useWeb ? 32 : 0;
        console.log('plantuml registerInternal: vxOptions.webPlantUml=', window.vxOptions.webPlantUml,
                    'useWeb=', this.useWeb, 'workerId=', this.id);
    }

    initialize(p_callback, p_failureCallback = null) {
        if (super.initialized) {
            return true;
        }

        if (!!window.vxOptions.plantUmlWebService) {
            this.serverUrl = window.vxOptions.plantUmlWebService;
            console.log('override PlantUml Web service', this.serverUrl);
        }

        return super.initialize(p_callback, p_failureCallback);
    }

    initializeRenderer() {
        if (this.useWeb
            && (typeof Zopfli === 'undefined'
                || typeof Zopfli.RawDeflate !== 'function'
                || typeof encode64_ !== 'function')) {
            throw new Error('PlantUML web libraries are not available');
        }
    }

    // Interface 1.
    render(p_node, p_format) {
        this.format = p_format;

        super.render(p_node, p_classList);
    }

    // Interface 2.
    renderCodeNodes(p_format) {
        if (window.vxOptions.protectedView) {
            for (const node of this.vxcore.getWorker('markdownit').getCodeNodes(this.langs)) {
                node.textContent = '[PlantUML blocked in protected notes: no bundled renderer]';
            }
            this.finishWork();
            return;
        }
        this.format = p_format;

        super.renderCodeNodes();
    }

    async renderOne(p_node, p_idx) {
        const generation = this.passGeneration;
        const isCurrent = () => this.passActive && this.passGeneration === generation;
        const format = this.format;
        try {
            const pages = await this.renderPages(format, p_node.textContent, isCurrent);
            if (isCurrent()) {
                this.handlePlantUmlResult(p_node, format, pages);
            }
        } catch (p_error) {
            if (isCurrent()) {
                this.reportProblem('PlantUML rendering failed', p_error);
                if (p_error.pages && p_error.pages.length > 0) {
                    this.handlePlantUmlResult(p_node, format, p_error.pages, p_error.message);
                } else {
                    p_node.textContent = '[PlantUML rendering failed: could not load all pages]\n'
                                         + p_node.textContent;
                }
            }
        } finally {
            if (isCurrent()) {
                this.finishRenderingOne();
            }
        }
    }

    // The renderer, not a source-text count, determines the number of pages:
    // newpage can come from an include, a macro, or a conditional branch.
    async renderPages(p_format, p_text, p_isCurrent = () => true) {
        const pages = [];
        const fail = (p_message) => {
            const error = new Error(p_message);
            error.pages = pages;
            throw error;
        };
        // A custom server/command may ignore the index and return images forever.
        // Fail visibly rather than hanging the viewer or silently truncating it.
        const maxPages = 256;
        const renderPage = (p_pageFormat, p_index) => new Promise((p_resolve) => {
            const callback = (p_format, p_data, p_success) => {
                p_resolve({ data: p_data, success: p_success });
            };
            if (this.useWeb) {
                this.renderOnline(this.serverUrl, p_pageFormat, p_text, callback, p_index);
            } else {
                this.renderLocal(p_pageFormat, p_text, callback, p_index);
            }
        });
        let firstSvg = null;
        let singlePage = false;
        if (this.useWeb) {
            firstSvg = await renderPage('svg', 0);
            // newpage belongs to sequence diagrams. Some public servers ignore
            // the index for other types and return the first image indefinitely.
            // Preserve their single-image behavior; older SVGs without a type
            // continue through the indexed protocol and its bounded error path.
            const type = /data-diagram-type="([^"]+)"/.exec(firstSvg.data);
            singlePage = type && type[1] !== 'SEQUENCE';
        }
        for (let imageIndex = 0; imageIndex < maxPages && p_isCurrent(); ++imageIndex) {
            const result = imageIndex === 0 && p_format === 'svg' && firstSvg
                ? firstSvg : await renderPage(p_format, imageIndex);
            if (!result.success) {
                // Keep the renderer's syntax-error image for a failed first page.
                if (imageIndex === 0 && result.data) {
                    return [result.data];
                }
                fail('Failed to render PlantUML page ' + (imageIndex + 1));
            }
            if (!result.data) {
                if (pages.length === 0) {
                    fail('PlantUML returned no image');
                }
                return pages;
            }
            pages.push(result.data);
            if (singlePage) {
                return pages;
            }
        }
        if (p_isCurrent()) {
            fail('PlantUML page limit (' + maxPages + ') reached');
        }
        return null;
    }

    // In-place previews accept one raster image. Stack the renderer's pages
    // without changing its source (ignore newpage is not valid for every diagram).
    renderText(p_text, p_callback) {
        if (window.vxOptions.protectedView) {
            p_callback('png', '');
            return;
        }
        const render = () => this.renderPages('png', p_text)
            .then((p_pages) => this.combinePages(p_pages))
            .then((p_data) => p_callback('png', p_data), (p_error) => {
                this.reportProblem('PlantUML preview failed', p_error);
                p_callback('png', '');
            });
        if (this.initialize(render, () => p_callback('png', ''))) {
            render();
        }
    }

    async combinePages(p_pages) {
        if (p_pages.length === 1) {
            return p_pages[0];
        }
        const images = await Promise.all(p_pages.map((p_data) => new Promise((p_resolve, p_reject) => {
            const image = new Image();
            image.onload = () => p_resolve(image);
            image.onerror = () => p_reject(new Error('Invalid PlantUML preview image'));
            image.src = 'data:image/png;base64,' + p_data;
        })));
        const canvas = document.createElement('canvas');
        canvas.width = Math.max(...images.map((p_image) => p_image.naturalWidth));
        canvas.height = images.reduce((p_height, p_image) => p_height + p_image.naturalHeight, 0);
        const context = canvas.getContext('2d');
        if (!context) {
            throw new Error('Could not allocate PlantUML preview canvas');
        }
        let top = 0;
        for (const image of images) {
            context.drawImage(image, 0, top);
            top += image.naturalHeight;
        }
        const dataUrl = canvas.toDataURL('image/png');
        if (dataUrl === 'data:,') {
            throw new Error('PlantUML preview exceeds the canvas size limit');
        }
        return dataUrl.substring(dataUrl.indexOf(',') + 1);
    }

    // A helper function to render PlantUml online.
    // Send request to @p_serverUrl to render @p_text as format @p_format.
    renderOnline(p_serverUrl, p_format, p_text, p_callback, p_imageIndex = 0) {
        const url = this.getPlantUMLOnlineUrl(p_serverUrl, p_format, p_text, p_imageIndex);
        Utils.httpGet(url, p_format === 'png' ? 'blob' : 'text', (p_resp, p_request) => {
            const contentType = p_request.getResponseHeader('Content-Type') || '';
            const mime = p_format === 'svg' ? 'image/svg+xml' : 'image/' + p_format;
            const isImage = contentType.split(';')[0].trim().toLowerCase() === mime;
            // PlantUML Server returns HTTP 400 without an image for an index
            // beyond the final page. Transport/server errors are NOT end-of-pages.
            if (p_imageIndex > 0 && p_request.status === 400 && !isImage) {
                p_callback(p_format, '', true);
                return;
            }
            // Older deployments export an unchecked index and return a crash
            // image instead of HTTP 400. Recognize only the sequence-title index
            // failure, never a generic 509/crash (which could hide a missing page).
            if (p_imageIndex > 0 && p_request.status === 509 && isImage) {
                if (p_format === 'svg') {
                    const atPageBoundary = /java\.lang\.IndexOutOfBoundsException:/.test(p_resp)
                        && /net\.sourceforge\.plantuml\.sequencediagram\.SequenceDiagram\.getTitle\(/.test(p_resp);
                    p_callback(p_format, '', atPageBoundary);
                } else {
                    // Read the diagnostic as SVG; a PNG crash image is opaque.
                    this.renderOnline(p_serverUrl, 'svg', p_text, (p_format, p_data, p_success) => {
                        p_callback('png', '', p_success && !p_data);
                    }, p_imageIndex);
                }
                return;
            }
            const success = p_request.status >= 200 && p_request.status < 300;
            if (!p_resp || !isImage || (!success && p_request.status !== 400)) {
                p_callback(p_format, '', false);
                return;
            }
            if (p_format === 'svg') {
                p_callback(p_format, p_resp, success);
                return;
            }
            const reader = new FileReader();
            reader.onload = () => {
                const dataUrl = reader.result;
                const data = dataUrl.substring(dataUrl.indexOf(',') + 1);
                p_callback(p_format, data, success && data.length > 0);
            };
            reader.onerror = () => p_callback(p_format, '', false);
            reader.readAsDataURL(p_resp);
        }, 30000);
    }

    getPlantUMLOnlineUrl(p_serverUrl, p_format, p_text, p_imageIndex = 0) {
        // Cache compression independently of the page index.
        const key = p_serverUrl.length + ':' + p_serverUrl + '|'
                    + p_format.length + ':' + p_format + '|' + p_text;
        let url = this.urlCache.get(key);
        if (url === undefined) {
            const s = unescape(encodeURIComponent(p_text));
            const arr = [];
            for (let i = 0; i < s.length; i++) {
                arr.push(s.charCodeAt(i));
            }
            const compressed = new Zopfli.RawDeflate(arr).compress();
            url = p_serverUrl.replace(/\/+$/, '') + '/' + p_format + '/' + encode64_(compressed);
            this.urlCache.set(key, url);
        }
        if (p_imageIndex > 0) {
            const slash = url.lastIndexOf('/');
            return url.substring(0, slash) + '/' + p_imageIndex + url.substring(slash);
        }
        return url;
    }

    // A helper function to render PlantUml via local JAR.
    renderLocal(p_format, p_text, p_callback, p_imageIndex = 0) {
        let index = this.nextLocalGraphIndex++;
        if (window.vxGraphVerbose) console.log('plantuml renderLocal: workerId=', this.id, 'index=', index,
                    'format=', p_format, 'textLen=', p_text.length);
        this.vxcore.renderGraph(this.id,
            index,
            p_format,
            'puml',
            p_text,
            function(id, index, format, data, success) {
                if (window.vxGraphVerbose) console.log('plantuml renderLocal result: id=', id, 'index=', index,
                            'format=', format, 'dataLen=', data ? data.length : 0);
                p_callback(format, data, success);
            }, p_imageIndex);
    }

    handlePlantUmlResult(p_node, p_format, p_pages, p_error = '') {
        const obj = document.createElement('div');
        obj.classList.add(this.graphDivClass);
        for (const data of p_pages) {
            const page = document.createElement('div');
            page.classList.add('vx-plantuml-page');
            if (p_format === 'svg') {
                page.innerHTML = data;
                const svg = page.querySelector('svg');
                if (!svg) {
                    throw new Error('PlantUML returned invalid SVG');
                }
                Utils.renamespaceSvgIds(page, '-puml-' + this.nextSvgIndex++);
                window.vxImageViewer.setupSVGToView(svg, false);
            } else {
                const image = document.createElement('img');
                image.src = 'data:image/' + p_format + ';base64,' + data;
                page.appendChild(image);
                window.vxImageViewer.setupIMGToView(image);
            }
            obj.appendChild(page);
        }
        if (p_error) {
            const warning = document.createElement('p');
            warning.setAttribute('role', 'alert');
            warning.textContent = p_error + ' — diagram may be incomplete';
            obj.appendChild(warning);
        }
        Utils.checkSourceLine(p_node, obj);
        Utils.replaceNodeWithPreCheck(p_node, obj);
    }

}

window.vxcore.registerWorker(new PlantUml());
