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

    finishWork() {
        console.log('worker finished', this.name);
        this.vxcore.finishWorker(this.name);
    }
}
