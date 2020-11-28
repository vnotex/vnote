// Should be singleton for each renderer.
class GraphRenderer extends VxWorker {
    constructor() {
        super();

        this.initialized = false;
        this.graphIdx = 0;
        this.graphDivClass = '';

        // Nodes need to render.
        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;

        // Used for loading scripts dynamically.
        this.scriptFolderPath = Utils.parentFolder(document.currentScript.src);

        // Extra scripts that need to load dynamically.
        this.extraScripts = [];

        // Langs for this graph render to render.
        this.langs = [];
    }

    reset() {
        this.graphIdx = 0;
        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;
    }

    registerInternal() {
        this.vnotex.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes(this.vnotex.contentContainer);
        });

        this.vnotex.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
    }

    // Return ture if we could continue.
    // Initialize may load additional libraries dynamically, in which case we need
    // to suspend our execution for now and call p_callback() later.
    initialize(p_callback) {
        if (this.initialized) {
            return true;
        }

        console.info('render initialized:', this.name);

        this.initialized = true;
        if (this.extraScripts.length > 0) {
            Utils.loadScripts(this.extraScripts, p_callback);
            return false;
        }

        return true;
    }

    // Interface 1.
    // Fetch nodes with class @p_classList in @p_node and render as graph.
    render(p_node, p_classList) {
        // Collect nodes to render.
        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;
        p_classList.forEach((p_class) => {
            let nodes = p_node.getElementsByClassName(p_class);
            if (nodes.length == 0) {
                return;
            }

            for (let i = 0; i < nodes.length; ++i) {
                // Do we need to de-duplicate nodes?
                this.nodesToRender.push(nodes[i]);
            }
        });

        this.doRender(this.nodesToRender);
    }

    // Interface 2.
    // Get code nodes from markdownIt directly.
    renderCodeNodes(p_node) {
        this.nodesToRender = this.vnotex.getWorker('markdownit').getCodeNodes(this.langs);
        this.doRender();
    }

    doRender() {
        if (this.nodesToRender.length == 0) {
            this.finishWork();
            return;
        }

        if (!this.initialize(() => { this.renderNodes(); })) {
            return;
        }

        this.renderNodes();
    }

    renderNodes() {
        this.nodesToRender.forEach((p_nodeToRender) => {
            this.renderOne(p_nodeToRender, this.graphIdx++);
        });
    }

    // Render @p_node as a graph.
    // Return true on success.
    renderOne(p_node, p_idx) {
        return false;
    }

    // Called when finishing rendering one node.
    finishRenderingOne() {
        if (++this.numOfRenderedNodes == this.nodesToRender.length) {
            this.nodesToRender = [];
            this.numOfRenderedNodes = 0;
            this.finishWork();
        }
    }
}
