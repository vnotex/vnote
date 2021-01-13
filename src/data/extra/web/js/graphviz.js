class Graphviz extends GraphRenderer {
    constructor() {
        super();

        this.name = 'graphviz';

        this.graphDivClass = 'vx-graphviz-graph';

        this.extraScripts = [this.scriptFolderPath + '/viz.js/viz.js',
                             this.scriptFolderPath + '/viz.js/lite.render.js'];

        this.viz = null;

        this.format = 'svg';

        this.langs = ['dot'];
    }

    registerInternal() {
        this.vnotex.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(this.vnotex.contentContainer,
                                 window.vxOptions.transformSvgToPngEnabled ? 'png' : 'svg');
        });

        this.vnotex.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
    }

    initialize(p_callback) {
        return super.initialize(() => {
            this.viz = new Viz();
            p_callback();
        });
    }

    // Interface 1.
    render(p_node, p_classList, p_format) {
        this.format = p_format;

        super.render(p_node, p_classList);
    }

    // Interface 2.
    renderCodeNodes(p_node, p_format) {
        this.format = p_format;

        super.renderCodeNodes(p_node);
    }

    renderOne(p_node, p_idx) {
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

    // Render a graph from @p_text in SVG format.
    // p_callback(svgNode).
    renderText(p_text, p_callback) {
        let func = () => {
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

window.vnotex.registerWorker(new Graphviz());
