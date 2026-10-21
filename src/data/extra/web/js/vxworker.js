// Worker base class.
class VxWorker {
    constructor() {
        this.name = '';
        this.vxcore = null;

        if (!window.vxWorkerId) {
            window.vxWorkerId = 1;
        }
        this.id = window.vxWorkerId++;
    }

    // Called when registering this worker.
    register(p_vxcore) {
        this.vxcore = p_vxcore;

        this.registerInternal();
    }

    registerInternal() {
        console.warning('RegisterInternal of VxWorker subclass is not implemented', this.name);
    }

    beginWork(p_generation) {
        this.workGeneration = p_generation;
    }

    isCurrentWork(p_generation) {
        return p_generation === this.workGeneration
            && p_generation === this.vxcore.documentGeneration;
    }

    finishWork(p_generation = this.workGeneration) {
        if (!this.isCurrentWork(p_generation)) {
            return;
        }
        this.workGeneration = null;
        this.vxcore.finishWorker(this.name, p_generation);
    }
}
