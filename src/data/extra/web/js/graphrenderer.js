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

        // Timing of one render pass; see reportTiming().
        this.startMs = 0;
        this.dispatchDoneMs = 0;
        this.completionMs = [];

        // How many renderOne() calls may be in flight at once during a read-mode
        // pass. 0 means unbounded, which is the historical behaviour and the right
        // answer for the I/O-bound renderers (web/local PlantUml, local Graphviz):
        // they land 200 results within a few hundred ms precisely because all the
        // requests are outstanding together.
        //
        // The CPU-bound in-page renderers (Mermaid, viz.js, flowchart, WaveDrom)
        // override this with a small number. Their promise continuations otherwise
        // monopolise the microtask queue for the whole pass and the compositor
        // never gets a rendering opportunity, so nothing appears until the last
        // diagram is done. Bounding the batch and yielding a MACROTASK between
        // batches does not make the pass faster - it makes it visible.
        //
        // Deliberately accepted cost: each diagram replaces its <pre> block as it
        // lands, so the document now reflows repeatedly over a few seconds instead
        // of once at the end, and a reader scrolled into the middle sees content
        // shift. That is judged better than a frozen page - but it IS the trade,
        // not an oversight.
        this.concurrencyLimit = 0;

        // Read-mode pass state; see renderNodes().
        this.passActive = false;
        this.passGeneration = 0;
        this.nextNodeIndex = 0;
        this.pendingInBatch = 0;
        this.inDispatch = false;
        this.nextBatchScheduled = false;

        // True between doRender() handing control to an asynchronous initialize()
        // and renderNodes() actually starting. It marks the window in which THIS
        // renderer owes a finishWork() but has not yet armed the pass machinery
        // that would deliver it; see initialize().
        this.initPendingForPass = false;

        // Used for loading scripts dynamically.
        this.scriptFolderPath = Utils.parentFolder(document.currentScript.src);

        // Extra scripts that need to load dynamically.
        this.extraScripts = [];

        // Langs for this graph render to render.
        this.langs = [];

        // Rendered-artifact cache with in-flight coalescing. Only the subclasses
        // that opt in construct one (Mermaid today); a renderer that cannot
        // enumerate every input varying its output, or whose output cannot be
        // safely inserted twice, leaves this null and pays nothing for it.
        this.graphCache = null;
    }

    reset() {
        // reset() is called from the basicMarkdownRendered handler, i.e. at the
        // start of the next round. If a pass is still live at that point the
        // serialisation invariant in MarkdownViewerCore.setMarkdownText() has been
        // violated; say so here, because clearing the state below is what would
        // otherwise hide it from the check in renderNodes().
        if (this.passActive) {
            console.error('graph render pass reset while still live', this.name);
        }

        this.graphIdx = 0;
        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;

        // Timing of one render pass. See reportTiming().
        this.startMs = 0;
        this.dispatchDoneMs = 0;
        this.completionMs = [];

        this.passActive = false;
        // Retires every callback still outstanding from the previous pass, so a
        // late completion cannot be counted against the new one.
        ++this.passGeneration;
        this.nextNodeIndex = 0;
        this.pendingInBatch = 0;
        this.inDispatch = false;
        this.nextBatchScheduled = false;
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.reset();
            this.renderCodeNodes();
        });

        this.vxcore.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
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
            Utils.loadScripts(this.extraScripts, () => {
                // p_callback here is the SUBCLASS's wrapper, and that wrapper
                // dereferences the library that was just loaded - Mermaid.initialize
                // calls mermaid.initialize(), Graphviz.initialize does `new Viz()`.
                // If the script failed to load, those throw a ReferenceError before
                // ever reaching renderNodes(), so without this guard a failed script
                // still deadlocks the viewer: no pass is armed, and nothing will
                // ever call finishWork(). Utils.loadScript's onerror arm is only
                // half the fix; this is the other half.
                try {
                    p_callback();
                } catch (p_err) {
                    console.error('renderer initialization failed', this.name, p_err);
                    this.abandonPendingPass();
                }
            });
            return false;
        }

        return true;
    }

    // Release the finishWork() debt taken on by doRender() when initialization
    // never got far enough to arm the pass machinery. A no-op for the in-place
    // preview entry points (renderText), which own no pass and must NOT finish one.
    abandonPendingPass() {
        if (!this.initPendingForPass) {
            return;
        }

        this.initPendingForPass = false;
        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;
        this.finishWork();
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

        this.doRender();
    }

    // Interface 2.
    // Get code nodes from markdownIt directly.
    renderCodeNodes() {
        this.nodesToRender = this.vxcore.getWorker('markdownit').getCodeNodes(this.langs);
        this.numOfRenderedNodes = 0;
        this.doRender();
    }

    doRender() {
        if (this.nodesToRender.length == 0) {
            this.finishWork();
            return;
        }

        // From here until renderNodes() runs, this renderer owes exactly one
        // finishWork() that no pass state is yet tracking. See abandonPendingPass().
        this.initPendingForPass = true;
        if (!this.initialize(() => {
                this.initPendingForPass = false;
                this.renderNodes();
            })) {
            return;
        }

        this.initPendingForPass = false;
        this.renderNodes();
    }

    renderNodes() {
        // The serialisation invariant in MarkdownViewerCore.setMarkdownText() means a
        // second pass cannot start while this one is live. Assert it rather than
        // building per-pass context objects for a case the architecture prevents - if
        // this ever fires, that is the signal to build the heavier machinery.
        if (this.passActive) {
            console.error('graph render pass started while one is still live', this.name);
        }

        // Timing, reported by reportTiming() when the last node lands.
        //
        // console.info() because WebPage::javaScriptConsoleMessage() forwards
        // only that level, into the vnote.web.js logging category - so this
        // costs nothing unless someone asks for it with
        // QT_LOGGING_RULES="vnote.web.js=true".
        this.startMs = Date.now();
        this.dispatchDoneMs = 0;
        this.completionMs = [];

        this.passActive = true;
        ++this.passGeneration;
        this.nextNodeIndex = 0;
        this.pendingInBatch = 0;
        this.nextBatchScheduled = false;

        this.dispatchBatch();
    }

    // Fill the concurrency window, then return. Refilled from finishRenderingOne()
    // as each node completes, so this is a SLIDING WINDOW rather than a barrier:
    // waiting for a whole batch to drain would make every batch cost its slowest
    // member, which for the I/O-bound renderers (web PlantUml at limit 32, 200
    // nodes) turns overlapping round trips into seven serialized ones.
    dispatchBatch() {
        if (!this.passActive) {
            return;
        }

        const total = this.nodesToRender.length;
        const limit = this.concurrencyLimit > 0 ? this.concurrencyLimit : total;

        // renderOne() may complete synchronously (a validation failure, a renderer
        // that is not async at all). finishRenderingOne() must not start the next
        // batch from underneath us while we are still filling this one.
        this.inDispatch = true;
        let launched = 0;
        // Two independent caps, and both are load-bearing:
        // - pendingInBatch < limit is the concurrency window, which is what bounds
        //   an ASYNC renderer.
        // - launched < limit bounds this TASK, which is what bounds a SYNCHRONOUS
        //   renderer: it completes inside renderOne(), so pendingInBatch drops
        //   straight back and would otherwise let this loop run the whole document
        //   in one task - exactly the frozen page being fixed.
        // Re-read passActive and the node list every iteration too: the node that
        // completes the pass clears nodesToRender from under us, and a stale length
        // would dispatch undefined nodes past the end of a finished pass.
        while (this.passActive
               && this.nextNodeIndex < this.nodesToRender.length
               && launched < limit
               && this.pendingInBatch < limit) {
            const nodeToRender = this.nodesToRender[this.nextNodeIndex++];
            ++this.pendingInBatch;
            ++launched;
            const generation = this.passGeneration;
            try {
                const outcome = this.renderOne(nodeToRender, this.graphIdx++);

                // CONTRACT: renderOne() either calls finishRenderingOne() (however
                // it ends), or it fails. An async renderOne() reports failure by
                // REJECTING, not by throwing synchronously, so the catch below
                // never sees it. Without this arm a single rejection - anything
                // thrown outside the subclass's own try block - would leave the node
                // pending forever, which under batching also stops every remaining
                // batch and deadlocks the viewer for the rest of the session.
                //
                // The generation check retires callbacks belonging to a pass that
                // has already been superseded.
                if (outcome && typeof outcome.then === 'function') {
                    outcome.then(undefined, (p_err) => {
                        console.error('failed to render graph node', this.name, p_err);
                        if (this.passGeneration === generation) {
                            this.finishRenderingOne();
                        }
                    });
                }
            } catch (p_err) {
                // A synchronously throwing renderOne(), same reasoning.
                console.error('failed to render graph node', this.name, p_err);
                this.finishRenderingOne();
            }
        }
        this.inDispatch = false;

        if (!this.passActive) {
            // The pass completed inside the loop above.
            return;
        }

        if (this.nextNodeIndex >= total) {
            // Every renderOne() has been STARTED, not finished: none of them is
            // awaited, so this measures only how long it takes to fire them all.
            // The gap between this and the first completion is the real work.
            this.dispatchDoneMs = Date.now();
        }

        this.scheduleNextBatch();
    }

    // A macrotask, deliberately: a microtask (Promise.resolve().then) gives the
    // compositor nothing, because the microtask queue is drained before the browser
    // ever considers painting. setTimeout rather than requestAnimationFrame because
    // rAF does not fire in a hidden window, and a pass that never resumes never calls
    // finishWork() - which deadlocks the viewer permanently.
    scheduleNextBatch() {
        if (!this.passActive || this.nextBatchScheduled) {
            return;
        }

        if (this.nextNodeIndex >= this.nodesToRender.length) {
            return;
        }

        // Nothing to refill yet - the window is still full.
        const limit = this.concurrencyLimit > 0 ? this.concurrencyLimit
                                                : this.nodesToRender.length;
        if (this.pendingInBatch >= limit) {
            return;
        }

        // Collapses the N completions of one window into a single continuation.
        this.nextBatchScheduled = true;
        setTimeout(() => {
            this.nextBatchScheduled = false;
            this.dispatchBatch();
        }, 0);
    }

    // Where a render pass actually spent its time.
    //
    // The interesting number is not the total - it is the ARRIVAL CURVE. If
    // completions are spread evenly the renderers are producing results
    // steadily and only the paint is batched; if they all land at the end, the
    // work itself is batched and no amount of paint scheduling will help.
    reportTiming() {
        const count = this.completionMs.length;
        if (count === 0) {
            return;
        }

        const total = this.completionMs[count - 1];
        const first = this.completionMs[0];
        // Nearest-rank: idx = min(n - 1, floor(p * n)). completionMs is
        // monotonically non-decreasing by construction, so it is already
        // sorted. The SAME rule is used by GraphPreviewer.perfQuantiles() and
        // by PreviewHelper::perfQuartiles() in C++; change one, change all
        // three, or a p50 stops meaning the same thing across the summaries
        // these logs exist to be compared against.
        const quartile = (p_f) => this.completionMs[Math.min(count - 1,
                                                             Math.floor(count * p_f))];

        // The largest gap between consecutive completions. A big one means the
        // pass stalled - waiting on a process or a server - rather than being
        // CPU bound in the page.
        let maxGap = 0;
        for (let i = 1; i < count; ++i) {
            maxGap = Math.max(maxGap, this.completionMs[i] - this.completionMs[i - 1]);
        }

        console.info('graph timing', this.name,
                     'nodes=' + count,
                     // Wall clock, so this pass can be overlaid with the
                     // edit-mode in-place summary and the C++ lifecycle
                     // markers. Everything else here is relative to startMs.
                     'startEpochMs=' + this.startMs,
                     'endEpochMs=' + (this.startMs + total),
                     'dispatch=' + (this.dispatchDoneMs - this.startMs) + 'ms',
                     'first=' + first + 'ms',
                     'p25=' + quartile(0.25) + 'ms',
                     'p50=' + quartile(0.5) + 'ms',
                     'p75=' + quartile(0.75) + 'ms',
                     'last=' + total + 'ms',
                     'maxGap=' + maxGap + 'ms',
                     'limit=' + this.concurrencyLimit,
                     // Only the renderers that opted into the cache have one, and a
                     // constant "invocations=0" would misrepresent the others in the
                     // very log used to validate it.
                     this.graphCache ? 'cache: ' + this.graphCache.statsString() : '');
    }

    // Render @p_node as a graph.
    // Return true on success.
    renderOne(p_node, p_idx) {
        return false;
    }

    // Called when finishing rendering one node.
    finishRenderingOne() {
        // A pass can only ever see as many completions as it has DISPATCHED.
        // Subclasses call this directly with no generation of their own (the
        // rejection arm in dispatchBatch() is the only site that can check
        // one), so a callback left over from a retired pass - an outstanding
        // `await mermaid.render`, an in-flight PlantUml round trip - would
        // otherwise be counted against the live pass. Because completePass()
        // fires on `>= nodesToRender.length`, that inflated count would end the
        // pass early, clear nodesToRender, and silently drop every node not yet
        // dispatched while still reporting the pass as finished. Enforcing
        // numOfRenderedNodes <= nextNodeIndex makes that impossible: the pass
        // can now only complete once everything really was dispatched.
        if (this.numOfRenderedNodes >= this.nextNodeIndex) {
            console.error('graph render completion from a retired pass', this.name);
            return;
        }

        // Milliseconds since the pass started. Recorded for every node, so the
        // arrival curve is available even when a pass never completes.
        if (this.startMs > 0) {
            this.completionMs.push(Date.now() - this.startMs);
        }

        if (this.pendingInBatch > 0) {
            --this.pendingInBatch;
        }

        if (++this.numOfRenderedNodes >= this.nodesToRender.length) {
            this.completePass();
            return;
        }

        // Refill the window as soon as a slot frees up, not once the whole window
        // has drained. While inDispatch the loop above is still filling it and will
        // schedule on its way out.
        if (!this.inDispatch) {
            this.scheduleNextBatch();
        }
    }

    // The single exit of a read-mode pass. Guarded by passActive so that a renderer
    // which calls finishRenderingOne() one time too many cannot call finishWork()
    // twice - a double decrement of numOfOngoingWorkers is as fatal as a missing one.
    completePass() {
        if (!this.passActive) {
            return;
        }

        this.passActive = false;
        if (this.dispatchDoneMs === 0) {
            this.dispatchDoneMs = Date.now();
        }

        // Diagnostics must never be able to cost the finishWork() below. Anything
        // thrown between here and that call deadlocks the viewer permanently, and
        // reportTiming() reaches into this.graphCache - which a half-updated
        // %APPDATA%/web/js (the stamp is dropped BEFORE the copy, and the copy is
        // tolerant of per-file failures) can leave as an older GraphCache without
        // statsString().
        try {
            this.reportTiming();
        } catch (p_err) {
            console.error('failed to report graph timing', this.name, p_err);
        }

        this.nodesToRender = [];
        this.numOfRenderedNodes = 0;
        this.nextNodeIndex = 0;
        this.pendingInBatch = 0;
        this.finishWork();
    }
}
