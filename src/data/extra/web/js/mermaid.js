class Mermaid extends GraphRenderer {
    constructor() {
        super();

        this.name = 'mermaid';

        this.graphDivClass = 'vx-mermaid-graph';

        this.extraScripts = [this.scriptFolderPath + '/mermaid/mermaid.min.js'];

        // default/dark/forest/neutral.
        this.theme = 'default';

        this.langs = ['mermaid'];
    }

    initialize(p_callback) {
        return super.initialize(() => {
           mermaid.mermaidAPI.initialize({
               startOnLoad: false,
               theme: this.theme
           });
            p_callback();
        });
    }

    // Render @p_node as Mermaid graph.
    // Return true on success.
    renderOne(p_node, p_idx) {
        let graphSvg = null;
        try {
            graphSvg = mermaid.mermaidAPI.render('vx-mermaid-graph-' + p_idx,
                                                 p_node.textContent,
                                                 function() {});
        } catch (p_err) {
            console.error('failed to render Mermaid', p_err);
            // Clean the container element, or Mermaid won't render the graph with
            // the same id.
            let graphNode = document.getElementById('vx-mermaid-graph-' + p_idx);
            if (graphNode) {
                let parentNode = graphNode.parentElement;
                parentNode.outerHTML = '';
                delete graphNode.parentElement;
            }
            this.finishRenderingOne();
            return false;
        }

        if (!graphSvg) {
            this.finishRenderingOne();
            return false;
        }

        let graphDiv = document.createElement('div');
        graphDiv.classList.add(this.graphDivClass);
        try {
            graphDiv.innerHTML = graphSvg;
            window.vxImageViewer.setupSVGToView(graphDiv.children[0], true);
        } catch (p_err) {
            console.error('incorrect graph SVG definition', p_err);
            this.finishRenderingOne();
            return false;
        }

        Utils.checkSourceLine(p_node, graphDiv);

        Utils.replaceNodeWithPreCheck(p_node, graphDiv);

        this.finishRenderingOne();
        return true;
    }

    // Render a graph from @p_text.
    // Will append a div to @p_container and return the div.
    renderTextInternal(p_container, p_text, p_idx) {
        let graphSvg = null;
        try {
            graphSvg = mermaid.mermaidAPI.render('vx-mermaid-graph-stand-alone-' + p_idx,
                                                 p_text,
                                                 function() {});
        } catch (p_err) {
            console.error('failed to render Mermaid', p_err);
            // Clean the container element, or Mermaid won't render the graph with
            // the same id.
            let graphNode = document.getElementById('vx-mermaid-graph-stand-alone-' + p_idx);
            if (graphNode) {
                let parentNode = graphNode.parentElement;
                parentNode.outerHTML = '';
                delete graphNode.parentElement;
            }
            return null;
        }

        if (!graphSvg) {
            return null;
        }

        let graphDiv = document.createElement('div');
        try {
            graphDiv.innerHTML = graphSvg;
        } catch (p_err) {
            console.error('incorrect graph SVG definition', p_err);
            return null;
        }

        p_container.appendChild(graphDiv);

        return graphDiv;
    }

    // p_callback(graphDiv).
    renderText(p_container, p_text, p_idx, p_callback) {
        if (!this.initialize(() => {
                let graphDiv = this.renderTextInternal(p_container, p_text, p_idx);
                p_callback(graphDiv);
            })) {
            return;
        }

        let graphDiv = this.renderTextInternal(p_container, p_text, p_idx);
        p_callback(graphDiv);
    }
}

window.vnotex.registerWorker(new Mermaid());
