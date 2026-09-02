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

        // (serverUrl, format, text) -> encoded URL. getPlantUMLOnlineUrl() runs
        // Zopfli synchronously, which is what makes the dispatch loop cost 2.6 s for
        // 200 diagrams; a document that repeats a diagram pays it once.
        this.urlCache = new LruCache(256);
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(window.vxOptions.transformSvgToPngEnabled ? 'png' : 'svg');
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
        this.format = p_format;

        super.renderCodeNodes();
    }

    renderOne(p_node, p_idx) {
        let func = function(p_plantUml, p_node) {
            let plantUml = p_plantUml;
            let node = p_node;
            return function(p_format, p_data) {
                plantUml.handlePlantUmlResult(node, p_format, p_data);
            };
        };

        if (this.useWeb) {
            this.renderOnline(this.serverUrl,
                              this.format,
                              p_node.textContent,
                              func(this, p_node));
        } else {
            if (window.vxGraphVerbose) console.log('plantuml renderOne(local): idx=', p_idx, 'format=', this.format,
                        'textLen=', p_node.textContent.length);
            this.renderLocal(this.format, p_node.textContent, func(this, p_node));
        }
        return true;
    }

    // Render a graph from @p_text as PNG for the in-place popup preview.
    // PNG (raster) is used instead of SVG because the preview is rasterized by
    // Qt's QSvgRenderer, which cannot render <foreignObject>/embedded HTML that
    // PlantUml emits for some labels (e.g. line breaks), leaving a blank popup.
    // p_callback(format, data).
    renderText(p_text, p_callback) {
        console.assert(this.useWeb, "renderText() should be called only when web PlantUml is enabled");

        let func = () => {
            this.renderOnline(this.serverUrl,
                              'png',
                              p_text,
                              p_callback);
        }

        if (!this.initialize(func, () => p_callback('png', ''))) {
            return;
        }

        func();
    }

    // A helper function to render PlantUml online.
    // Send request to @p_serverUrl to render @p_text as format @p_format.
    renderOnline(p_serverUrl, p_format, p_text, p_callback) {
        let url = this.getPlantUMLOnlineUrl(p_serverUrl, p_format, p_text);

        if (p_format == 'png') {
            Utils.httpGet(url, 'blob', function(p_resp) {
                // A transport failure hands back null. Report an empty result
                // rather than throwing, so the node still completes its pass.
                if (!p_resp) {
                    p_callback(p_format, '');
                    return;
                }

                let blob = p_resp;
                let reader = new FileReader();
                reader.onload = function () {
                    let dataUrl = reader.result;
                    let png = dataUrl.substring(dataUrl.indexOf(',') + 1);
                    p_callback(p_format, png);
                };
                reader.onerror = function () { p_callback(p_format, ''); };

                reader.readAsDataURL(blob);
            });
        } else if (p_format == 'svg') {
            Utils.httpGet(url, 'text', function(p_resp) {
                p_callback(p_format, p_resp || '');
            });
        }
    }

    getPlantUMLOnlineUrl(p_serverUrl, p_format, p_text) {
        // Length-prefixed, so no two different triples share a key.
        const key = p_serverUrl.length + ':' + p_serverUrl + '|'
                    + p_format.length + ':' + p_format + '|' + p_text;
        const cached = this.urlCache.get(key);
        if (cached !== undefined) {
            return cached;
        }

        let s = unescape(encodeURIComponent(p_text));
        let arr = [];
        for (let i = 0; i < s.length; i++) {
            arr.push(s.charCodeAt(i));
        }

        let compressor = new Zopfli.RawDeflate(arr);
        let compressed = compressor.compress();
        let url = p_serverUrl + "/" + p_format + "/" + encode64_(compressed);
        this.urlCache.set(key, url);
        return url;
    }

    // A helper function to render PlantUml via local JAR.
    renderLocal(p_format, p_text, p_callback) {
        let index = this.nextLocalGraphIndex++;
        if (window.vxGraphVerbose) console.log('plantuml renderLocal: workerId=', this.id, 'index=', index,
                    'format=', p_format, 'textLen=', p_text.length);
        this.vxcore.renderGraph(this.id,
            index,
            p_format,
            'puml',
            p_text,
            function(id, index, format, data) {
                if (window.vxGraphVerbose) console.log('plantuml renderLocal result: id=', id, 'index=', index,
                            'format=', format, 'dataLen=', data ? data.length : 0);
                p_callback(format, data);
            });
    }

    handlePlantUmlResult(p_node, p_format, p_result) {
        if (window.vxGraphVerbose) console.log('plantuml handlePlantUmlResult: format=', p_format,
                    'resultLen=', p_result ? p_result.length : 0,
                    'hasNode=', !!p_node);
        if (p_node && p_result.length > 0) {
            let obj = null;
            if (p_format == 'svg') {
                obj = document.createElement('div');
                obj.classList.add(this.graphDivClass);
                obj.innerHTML = p_result;
                window.vxImageViewer.setupSVGToView(obj.children[0], false);
            } else {
                obj = document.createElement('div');
                obj.classList.add(this.graphDivClass);

                let imgObj = document.createElement('img');
                obj.appendChild(imgObj);
                imgObj.src = "data:image/" + p_format + ";base64, " + p_result;
                window.vxImageViewer.setupIMGToView(imgObj);
            }

            Utils.checkSourceLine(p_node, obj);

            Utils.replaceNodeWithPreCheck(p_node, obj);
        }
        this.finishRenderingOne();
    }
}

window.vxcore.registerWorker(new PlantUml());
