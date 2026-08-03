// Maximum number of outline entries published to the C++ side. The Outline dock
// builds a full QAbstractItemModel tree from these, so an unbounded outline
// (some generated PDFs carry tens of thousands of bookmarks) is a UI freeze.
const VX_MAX_OUTLINE_ENTRIES = 5000;

class PdfViewerCore extends VXCore {
    constructor() {
        super();

        const scriptFolderPath = Utils.parentFolder(document.currentScript.src);
        this.workerSrc = scriptFolderPath + '/build/pdf.worker.js';

        // Destination objects of the current document, indexed by the `index`
        // field of the flat outline. The C++ side never sees a destination, only
        // its index here, so a stale index can never be resolved against a
        // different document as long as this is reset together with the outline.
        this.outlineDests = [];

        // The two asynchronous inputs of the publish rendezvous. See
        // publishOutline() for why this is plain state and not a Promise.
        this.outlineAdapter = null;
        this.pendingOutline = null;
    }

    initOnLoad() {
    }

    loadPdf(p_url) {
    }

    // === Outline ===

    // Collapse a raw bookmark title into a single-line label.
    static sanitizeOutlineTitle(p_title) {
        if (typeof p_title !== 'string') {
            return '';
        }
        // NUL characters would truncate the string on the C++ side; newlines and
        // tabs would break the single-line tree rows.
        return p_title.replace(/\0/g, '').replace(/\s+/g, ' ').trim();
    }

    // Flatten the PDF outline tree (as returned by PDFDocumentProxy.getOutline())
    // into the wire format shared with PdfViewerAdapter::Heading::fromJson():
    //
    //     { name: <string>, level: <1-based int>, index: <int> }
    //
    // ordered by a pre-order DFS. `index` addresses this.outlineDests, and is -1
    // for an entry with no destination (a url / action / attachment /
    // setOCGState bookmark). Such entries are still emitted: dropping them would
    // shift the level of their children in the published tree.
    //
    // Takes only the raw tree: the caller owns the document-staleness check (see
    // attachOutlineBridge), so passing the document in here would be misleading.
    buildOutline(p_rawOutline) {
        this.outlineDests = [];

        var flat = [];
        if (!p_rawOutline || !p_rawOutline.length) {
            return flat;
        }

        var self = this;
        var truncated = false;

        var walk = function(p_items, p_level) {
            if (!p_items || !p_items.length) {
                return;
            }
            for (var i = 0; i < p_items.length; ++i) {
                if (flat.length >= VX_MAX_OUTLINE_ENTRIES) {
                    truncated = true;
                    return;
                }

                var item = p_items[i];
                var dest = (item.dest === undefined || item.dest === null) ? null : item.dest;
                var destIndex = -1;
                if (dest !== null) {
                    // outlineDests is appended to ONLY for real destinations, so
                    // `index` is a dense index into it.
                    destIndex = self.outlineDests.length;
                    self.outlineDests.push(dest);
                }

                flat.push({
                    name: PdfViewerCore.sanitizeOutlineTitle(item.title),
                    level: p_level,
                    index: destIndex
                });

                walk(item.items, p_level + 1);
                if (truncated) {
                    return;
                }
            }
        };

        walk(p_rawOutline, 1);

        if (truncated) {
            console.warn('PDF outline truncated to ' + VX_MAX_OUTLINE_ENTRIES + ' entries');
        }

        return flat;
    }

    // Jump to the destination addressed by @p_index. Out-of-range and -1 are
    // inert no-ops, which is what makes a destination-less bookmark harmless.
    gotoOutlineItem(p_index) {
        if (typeof p_index !== 'number' || p_index < 0 || p_index >= this.outlineDests.length) {
            return;
        }

        var dest = this.outlineDests[p_index];
        if (dest === undefined || dest === null) {
            return;
        }

        window.PDFViewerApplication.pdfLinkService.goToDestination(dest);
    }

    // === The publish rendezvous ===
    //
    // The QWebChannel callback that hands us the adapter and the pdf.js
    // 'documentloaded' event that hands us the outline are independent
    // asynchronous chains; EITHER can win. A bare `if (this.outlineAdapter)`
    // guard around the publish would discard the outline forever when the
    // document wins, because 'documentloaded' does not fire again for that
    // document.
    //
    // So: record whichever arrives first, publish once both are present. No
    // promises, no microtask ordering, idempotent, synchronously testable.

    setOutlineAdapter(p_adapter) {
        this.outlineAdapter = p_adapter;
        this.publishOutline();
    }

    setPendingOutline(p_flat) {
        this.pendingOutline = p_flat;
        this.publishOutline();
    }

    publishOutline() {
        if (!this.outlineAdapter || !this.pendingOutline) {
            return;
        }

        var flat = this.pendingOutline;
        // Cleared BEFORE the call so a re-entrant setOutline is safe and the
        // outline is published exactly once. Note [] is truthy in JS, so a
        // legitimately empty outline is still published above.
        this.pendingOutline = null;
        this.outlineAdapter.setOutline(flat);
    }

    // Drop the outline of the document being replaced. Without this, both the
    // headings AND outlineDests of the previous document stay live, and a click
    // would hand an old destination to the new document's pdfLinkService.
    resetOutline() {
        this.outlineDests = [];
        this.setPendingOutline([]);
    }

    // Wire the outline pipeline onto a PDFViewerApplication. @p_app is injected
    // rather than read from window, which is what makes this testable.
    attachOutlineBridge(p_app) {
        var self = this;

        // 'pagesdestroy' is dispatched from PDFViewer.setDocument() ONLY when a
        // previous document exists, and close() (which open() awaits) triggers
        // it — so it fires exactly on replacement and never on the first load.
        //
        // Do NOT "simplify" this to 'documentinit': that is dispatched from a
        // DIFFERENT promise chain than 'documentloaded' (it additionally waits
        // on ViewHistory/IndexedDB), so it can arrive AFTER a good outline has
        // been published and would blank it permanently.
        p_app.eventBus.on('pagesdestroy', function() { self.resetOutline(); });
        p_app.eventBus.on('documenterror', function() { self.resetOutline(); });

        p_app.eventBus.on('documentloaded', function() {
            var doc = p_app.pdfDocument;
            doc.getOutline().then(function(p_raw) {
                if (p_app.pdfDocument !== doc) {
                    // The document was swapped while getOutline() was in flight.
                    return;
                }
                self.setPendingOutline(self.buildOutline(p_raw || []));
            }).catch(function(p_e) {
                console.error('failed to fetch PDF outline: ' + p_e);
            });
        });
    }
}

window.vxcore = new PdfViewerCore();
