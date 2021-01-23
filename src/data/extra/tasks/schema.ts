interface TaskConfiguration extends TaskDescription {
    /**
     * The configuration's version number
     */
    version?: '0.1.0';
}

interface TaskDescription {
    /**
     * The command to be executed. Can be an external program or a shell
     * command.
     */
    command?: string;

    /**
     * The task's name. Can be omitted.
     */
    label?: string;

    /**
     * The configuration of the available tasks.
     */
    tasks?: TaskDescription[];
}
