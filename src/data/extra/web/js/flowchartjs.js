// Does not support dynamically libraries loading.
class FlowchartJs extends GraphRenderer {
    constructor() {
        super();

        this.name = 'flowchartjs';

        this.graphDivClass = 'vx-flowchartjs-graph';

        this.langs = ['flow', 'flowchart'];
    }

    // Render @p_node as Flowchart.js graph.
    // Return true on success.
    renderOne(p_node, p_idx) {
        let graph = null;
        try {
            graph = flowchart.parse(p_node.textContent);
        } catch (p_err) {
            console.error('failed to render Flowchart.js', p_err);
            this.finishRenderingOne();
            return false;
        }

        if (!graph) {
            this.finishRenderingOne();
            return false;
        }

        // Create a div container.
        let graphDiv = document.createElement('div');
        graphDiv.id = 'vx-flowchartjs-graph-' + p_idx;
        graphDiv.classList.add(this.graphDivClass);

        Utils.checkSourceLine(p_node, graphDiv);

        let childNode = p_node;
        let parentNode = p_node.parentNode;
        if (parentNode.tagName.toLowerCase() == 'pre') {
            childNode = parentNode;
            parentNode = parentNode.parentNode;
        }
        parentNode.replaceChild(graphDiv, childNode);

        // Draw on it after adding div to page.
        try {
            graph.drawSVG(graphDiv.id);
            window.vxImageViewer.setupSVGToView(graphDiv.children[0], true);
        } catch (p_err) {
            console.error('failed to draw Flowchart.js SVG', p_err);
            parentNode.replaceChild(childNode, graphDiv);
            this.finishRenderingOne();
            return false;
        }

        this.finishRenderingOne();
        return true;
    }

    // Render a graph from @p_text.
    // Will append a div to @p_container and return the div.
    // p_callback(graphDiv).
    renderText(p_container, p_text, p_idx, p_callback) {
        let graph = null;

        try {
            graph = flowchart.parse(p_text);
        } catch (p_err) {
            console.error('failed to render Flowchart.js', p_err);
            graph = null;
        }

        if (!graph) {
            p_callback(null);
            return;
        }

        // Create a div container.
        let graphDiv = document.createElement('div');
        graphDiv.id = 'vx-flowchartjs-graph-stand-alone-' + p_idx;

        p_container.appendChild(graphDiv);

        // Draw on it after adding div to page.
        try {
            graph.drawSVG(graphDiv.id);
        } catch (p_err) {
            console.error('failed to draw Flowchart.js SVG', p_err);
            p_container.removeChild(graphDiv);
            return null;
        }

        this.fixStandAloneGraph(graphDiv.firstElementChild);

        p_callback(graphDiv);
    }

    // Raphael will reuse some global unique marker.
    // We need to insert it into the graph to make it stand-alone.
    // @p_graph: the <svg> node.
    fixStandAloneGraph(p_graph) {
        let markerBlock = document.getElementById('raphael-marker-block');
        if (!p_graph.contains(markerBlock)) {
            let clonedMarkerBlock = markerBlock.cloneNode(true);
            let defs = p_graph.getElementsByTagName('defs');
            if (defs.length > 0) {
                defs[0].insertAdjacentElement('afterbegin', clonedMarkerBlock);
            }
        }
    }
}

window.vnotex.registerWorker(new FlowchartJs());
