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
    }

    registerInternal() {
        this.vnotex.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(this.vnotex.contentContainer,
                                 window.vxOptions.transformSvgToPngEnabled ? 'png' : 'svg');
        });

        this.vnotex.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
    }


    // Interface 1.
    render(p_node, p_format) {
        this.format = p_format;

        super.render(p_node, p_classList);
    }

    // Interface 2.
    renderCodeNodes(p_node, p_format) {
        this.format = p_format;

        super.renderCodeNodes(p_node);
    }

    renderOne(p_node, p_idx) {
        let func = function(p_plantUml, p_node) {
            let plantUml = p_plantUml;
            let node = p_node;
            return function(p_format, p_data) {
                plantUml.handlePlantUmlResult(node, 0, p_format, p_data);
            };
        };

        this.renderOnline(this.serverUrl,
                          this.format,
                          p_node.textContent,
                          func(this, p_node));
        return true;
    }

    // Render a graph from @p_text in SVG format.
    // p_callback(format, data).
    renderText(p_text, p_callback) {
        let func = () => {
            this.renderOnline(this.serverUrl,
                              'svg',
                              p_text,
                              p_callback);
        }

        if (!this.initialize(func)) {
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
                let blob = p_resp;
                let reader = new FileReader();
                reader.onload = function () {
                    let dataUrl = reader.result;
                    let png = dataUrl.substring(dataUrl.indexOf(',') + 1);
                    p_callback(p_format, png);
                };

                reader.readAsDataURL(blob);
            });
        } else if (p_format == 'svg') {
            Utils.httpGet(url, 'text', function(p_resp) {
                p_callback(p_format, p_resp);
            });
        }
    }

    getPlantUMLOnlineUrl(p_serverUrl, p_format, p_text) {
        let s = unescape(encodeURIComponent(p_text));
        let arr = [];
        for (let i = 0; i < s.length; i++) {
            arr.push(s.charCodeAt(i));
        }

        let compressor = new Zopfli.RawDeflate(arr);
        let compressed = compressor.compress();
        let url = p_serverUrl + "/" + p_format + "/" + encode64_(compressed);
        return url;
    }

    handlePlantUmlResult(p_node, p_timeStamp, p_format, p_result) {
        if (p_node && p_result.length > 0) {
            let obj = null;
            if (p_format == 'svg') {
                obj = document.createElement('div');
                obj.classList.add(this.graphDivClass);
                obj.innerHTML = p_result;
                window.vxImageViewer.setupSVGToView(obj.children[0], false);
            } else {
                obj = document.createElement('img');
                obj.src = "data:image/" + p_format + ";base64, " + p_result;
                window.vxImageViewer.setupIMGToView(obj);
            }

            Utils.checkSourceLine(p_node, obj);

            Utils.replaceNodeWithPreCheck(p_node, obj);
        }
        this.finishRenderingOne();
    }
}

window.vnotex.registerWorker(new PlantUml());
