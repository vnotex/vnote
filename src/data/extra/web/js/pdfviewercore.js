class PdfViewerCore extends VXCore {
    constructor() {
        super();

        const scriptFolderPath = Utils.parentFolder(document.currentScript.src);
        this.workerSrc = scriptFolderPath + '/pdf.js/pdf.worker.min.js';
    }

    initOnLoad() {
        this.container = document.getElementById('vx-viewer-container');
    }

    loadPdf(p_url) {
        const eventBus = new pdfjsViewer.EventBus();

        // (Optionally) enable hyperlinks within PDF files.
        const pdfLinkService = new pdfjsViewer.PDFLinkService({
            eventBus,
        });

        // (Optionally) enable find controller.
        const pdfFindController = new pdfjsViewer.PDFFindController({
            eventBus,
            linkService: pdfLinkService,
        });

        const pdfViewer = new pdfjsViewer.PDFViewer({
            container: this.container,
            eventBus,
            linkService: pdfLinkService,
            findController: pdfFindController,
        });
        pdfLinkService.setViewer(pdfViewer);

        eventBus.on("pagesinit", function () {
            // We can use pdfViewer now, e.g. let's change default scale.
            pdfViewer.currentScaleValue = "page-width";
        });

        // Loading document.
        const loadingTask = pdfjsLib.getDocument({
            url: p_url,
            enableXfa: true,
        });
        (async function () {
            const pdfDocument = await loadingTask.promise;
            // Document loaded, specifying document for the viewer and
            // the (optional) linkService.
            pdfViewer.setDocument(pdfDocument);
            pdfLinkService.setDocument(pdfDocument, null);
        })();
    }
}

window.vxcore = new PdfViewerCore();
