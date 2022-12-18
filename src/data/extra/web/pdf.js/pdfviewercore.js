class PdfViewerCore extends VXCore {
    constructor() {
        super();

        const scriptFolderPath = Utils.parentFolder(document.currentScript.src);
        this.workerSrc = scriptFolderPath + '/build/pdf.worker.js';
    }

    initOnLoad() {
    }

    loadPdf(p_url) {
    }
}

window.vxcore = new PdfViewerCore();
