/*
    The main object that will be provided to all scripts in VNoteX.
    TODO: Maintain a list of workers.

    Events:
        - initialized()
        - ready()
*/

class VXCore extends EventEmitter {
    constructor() {
        super();

        this.kickedOff = false;

        this.initialized = false;

        this.os = VXCore.detectOS();

        window.addEventListener('load', () => {
            console.log('window load finished');

            this.initOnLoad();

            this.initialized = true;

            // Signal out.
            this.emit('initialized');
            this.emit('ready');
        });
    }

    static detectOS() {
        let osName="Unknown OS";
        if (navigator.appVersion.indexOf("Win")!=-1) {
            osName="Windows";
        } else if (navigator.appVersion.indexOf("Mac")!=-1) {
            osName="MacOS";
        } else if (navigator.appVersion.indexOf("X11")!=-1) {
            osName="UNIX";
        } else if (navigator.appVersion.indexOf("Linux")!=-1) {
            osName="Linux";
        }
        return osName
    }
}
