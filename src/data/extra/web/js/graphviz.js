class Graphviz extends GraphRenderer {
    constructor() {
        super();

        this.name = 'graphviz';

        this.graphDivClass = 'vx-graphviz-graph';

        this.extraScripts = [this.scriptFolderPath + '/viz.js/viz.js',
                             this.scriptFolderPath + '/viz.js/lite.render.js'];

        this.viz = null;

        this.format = 'svg';

        this.langs = ['dot', 'graphviz'];

        this.useWeb = true;

        this.nextLocalGraphIndex = 1;
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(window.vxOptions.transformSvgToPngEnabled ? 'png' : 'svg');
        });

        this.vxcore.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
        this.useWeb = window.vxOptions.webGraphviz;
        if (!this.useWeb) {
            this.extraScripts = [];
        }
    }

    initialize(p_callback) {
        return super.initialize(() => {
            if (this.useWeb) {
                this.viz = new Viz();
            }
            p_callback();
        });
    }

    // Interface 1.
    render(p_node, p_classList, p_format) {
        this.format = p_format;

        super.render(p_node, p_classList);
    }

    // Interface 2.
    renderCodeNodes(p_format) {
        this.format = p_format;

        super.renderCodeNodes();
    }

    renderOne(p_node, p_idx) {
        if (this.useWeb) {
            this.renderOnline(p_node, p_idx);
        } else {
            this.renderLocal(p_node);
        }
    }

    renderOnline(p_node, p_idx) {
        console.assert(this.viz);
        let func = function(p_graphviz, p_renderNode) {
            let graphviz = p_graphviz;
            let node = p_renderNode;
            return function(p_element) {
                if (node) {
                    let wrapperDiv = document.createElement('div');
                    wrapperDiv.classList.add(graphviz.graphDivClass);
                    wrapperDiv.appendChild(p_element);

                    Utils.checkSourceLine(p_node, wrapperDiv);

                    Utils.replaceNodeWithPreCheck(p_node, wrapperDiv);

                    if (graphviz.format === 'svg') {
                        window.vxImageViewer.setupSVGToView(p_element, false);
                    } else {
                        window.vxImageViewer.setupIMGToView(p_element);
                    }
                }
                graphviz.finishRenderingOne();
            };
        };

        if (this.format === 'svg') {
            this.viz.renderSVGElement(p_node.textContent)
                .then(func(this, p_node))
                .catch(function(p_err) {
                    console.error('failed to render Graphviz', p_err);
                });
        } else {
            this.viz.renderImageElement(p_node.textContent)
                .then(func(this, p_node))
                .catch(function(p_err) {
                    console.error('failed to render Graphviz', p_err);
                });

        }

        return true;
    }

    renderLocal(p_node) {
        let func = function(p_graphviz, p_renderNode) {
            let graphviz = p_graphviz;
            let node = p_renderNode;
            return function(format, data) {
                if (node && data.length > 0) {
                    let obj = null;
                    if (format == 'svg') {
                        obj = document.createElement('div');
                        obj.classList.add(graphviz.graphDivClass);
                        obj.innerHTML = data;
                        window.vxImageViewer.setupSVGToView(obj.children[0], false);
                    } else {
                        obj = document.createElement('div');
                        obj.classList.add(graphviz.graphDivClass);

                        let imgObj = document.createElement('img');
                        obj.appendChild(imgObj);
                        imgObj.src = "data:image/" + format + ";base64, " + data;
                        window.vxImageViewer.setupIMGToView(imgObj);
                    }

                    Utils.checkSourceLine(p_node, obj);

                    Utils.replaceNodeWithPreCheck(p_node, obj);
                }
                graphviz.finishRenderingOne();
            };
        };

        let callback = func(this, p_node);
        this.vxcore.renderGraph(this.id,
            this.nextLocalGraphIndex++,
            this.format,
            'dot',
            p_node.textContent,
            function(id, index, format, data) {
                callback(format, data);
            });
    }

    // Render a graph from @p_text in SVG format.
    // p_callback(svgNode).
    renderText(p_text, p_callback) {
        console.assert(this.useWeb, "renderText() should be called only when web Graphviz is enabled");

        let func = () => {
            if (!this.viz) {
                console.log("viz is not ready yet");
                return;
            }
            this.viz.renderSVGElement(p_text)
                .then(p_callback)
                .catch(function(err) {
                    console.error('failed to render Graphviz', err);
                    p_callback(null);
                });
        };

        if (!this.initialize(func)) {
            return;
        }

        func();
    }
}

window.vxcore.registerWorker(new Graphviz());
