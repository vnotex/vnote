class WaveDromRenderer extends GraphRenderer {
    constructor() {
        super();

        this.name = 'wavedrom';

        this.graphDivClass = 'vx-wavedrom-graph';

        this.extraScripts = [this.scriptFolderPath + '/wavedrom/theme-default.js',
                             this.scriptFolderPath + '/wavedrom/wavedrom.min.js'];

        this.langs = ['wavedrom', 'wave'];

        // Fully synchronous and in-page. See GraphRenderer.concurrencyLimit.
        this.concurrencyLimit = 8;
    }

    initializeRenderer() {
        if (typeof WaveDrom === 'undefined'
            || typeof WaveDrom.RenderWaveForm !== 'function'
            || typeof WaveSkin === 'undefined'
            || !Array.isArray(WaveSkin.default)) {
            throw new Error('WaveDrom library is not available');
        }
    }

    // Render @p_node as WaveDrom graph.
    // Return true on success.
    renderOne(p_node, p_idx) {
        // Create a div container.
        let graphDiv = document.createElement('div');
        graphDiv.id = 'vx-wavedrom-graph-' + p_idx;
        graphDiv.classList.add(this.graphDivClass);

        Utils.checkSourceLine(p_node, graphDiv);

        Utils.replaceNodeWithPreCheck(p_node, graphDiv);

        try {
            // ATTENTION: p_idx should start from 0 or style will be missing.
            WaveDrom.RenderWaveForm(p_idx,
                                    eval('(' + p_node.textContent + ')'),
                                    'vx-wavedrom-graph-');
            window.vxImageViewer.setupSVGToView(graphDiv.children[0], true);
        } catch (p_err) {
            console.error('failed to RenderWaveForm() for WaveDrom', p_err);
            this.finishRenderingOne();
            return false;
        }

        this.finishRenderingOne();
        return true;
    }

    // p_callback(graphDiv).
    renderText(p_container, p_text, p_idx, p_callback) {
        if (!this.initialize(() => {
                let graphDiv = this.renderTextInternal(p_container, p_text, p_idx);
                p_callback(graphDiv);
            }, () => p_callback(null))) {
            return;
        }

        let graphDiv = this.renderTextInternal(p_container, p_text, p_idx);
        p_callback(graphDiv);
    }

    // Render a graph from @p_text.
    // Will append a div to @p_container and return the div.
    renderTextInternal(p_container, p_text, p_idx) {
        // Create a div container.
        let graphDiv = document.createElement('div');
        graphDiv.id = 'vx-wavedrom-graph-stand-alone-' + p_idx + '0';

        p_container.appendChild(graphDiv);

        try {
            // Always use 0 as the index.
            WaveDrom.RenderWaveForm(0,
                                    eval('(' + p_text + ')'),
                                    'vx-wavedrom-graph-stand-alone-' + p_idx);
        } catch (p_err) {
            console.error('failed to RenderWaveForm() for WaveDrom', p_err);
            p_container.removeChild(graphDiv);
            return null;
        }

        return graphDiv;
    }
}

window.vxcore.registerWorker(new WaveDromRenderer());
