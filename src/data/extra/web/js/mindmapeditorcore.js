class MindMapEditorCore extends VXCore {
    constructor() {
        super();
    }

    initOnLoad() {
        let options = {
          el: '#vx-mindmap',
          direction: MindElixir.LEFT,
        }

        this.mind = new MindElixir(options);

        this.mind.bus.addListener('operation', operation => {
            if (operation === 'beginEdit') {
                return;
            }
            window.vxAdapter.notifyContentsChanged();
        });
    }

    saveData(p_id) {
        let data = this.mind.getAllDataString();
        window.vxAdapter.setSavedData(p_id, data);
    }

    setData(p_data) {
        if (p_data && p_data !== "") {
            this.mind.init(JSON.parse(p_data));
        } else {
            const data = MindElixir.new('New Topic')
            this.mind.init(data)
        }
        this.emit('rendered');
    }
}

window.vxcore = new MindMapEditorCore();
