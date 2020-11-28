(function() {
    let scriptNode = document.createElement('script');

    let scriptFolderPath = document.currentScript.src;
    scriptFolderPath = scriptFolderPath.substr(0, scriptFolderPath.lastIndexOf('/'));
    scriptNode.src = scriptFolderPath + '/prism/prism.min.js';
    scriptNode.setAttribute('data-manual', '');
    document.head.appendChild(scriptNode);
})();

class PrismRenderer extends VxWorker {
    constructor() {
        super();

        this.name = 'prism';

        this.initialized = false;
    }

    registerInternal() {
        this.vnotex.on('basicMarkdownRendered', () => {
            this.renderCodeNodes(this.vnotex.contentContainer);
        });
    }

    initialize() {
        if (this.initialized) {
            return;
        }

        this.initialized = true;

        let markdownIt = this.vnotex.getWorker('markdownit');
        Prism.plugins.filterHighlightAll.add((p_env) => {
            return !markdownIt.langsToSkipHighlight.has(p_env.language);
        });

        /*
        Prism.hooks.add('complete', () => {
            this.finishWork();
        });
        */
    }

    // Interface 1.
    // Fetch nodes to render from @p_node.
    render(p_node) {
        this.initialize();

        // We use querySelectorAll() to get a snapshot since other VxWorkers may
        // be changing the nodes, too.
        let codeNodes = p_node.querySelectorAll('pre code');
        this.doRender(p_node, codeNodes);
    }

    // Interface 2.
    // Get code nodes from markdownIt directly.
    renderCodeNodes(p_node) {
        this.initialize();

        let codeNodes = this.vnotex.getWorker('markdownit').getCodeNodes(null);
        this.doRender(p_node, codeNodes);
    }

    // Whether has class lang- or language-.
    hasLangSpecified(p_node) {
        for (let i = 0; i < p_node.classList.length; ++i) {
            let key = p_node.classList[i];
            if (key.startsWith('lang-') || key.startsWith('language-')) {
                return true;
            }
        }

        return false;
    }

    doRender(p_containerNode, p_nodes) {
        if (p_nodes.length > 0) {
            // Add `lang-txt` to code nodes without any class to let Prism catch them.
            for (let i = 0; i < p_nodes.length; ++i) {
                if (!this.hasLangSpecified(p_nodes[i])) {
                    p_nodes[i].classList.add('lang-txt');
                }
            }

            p_containerNode.classList.add('line-numbers');

            Prism.highlightAllUnder(p_containerNode, false /* async or not */);
        }

        this.finishWork();
    }
}

window.vnotex.registerWorker(new PrismRenderer());
