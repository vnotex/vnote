interface TaskConfiguration extends TaskDescription {
    /**
     * The configuration's version number
     */
    version?: '0.1.2';
}

interface TaskDescription {
    /**
     * The type of a custom task. Tasks of type "shell" are executed
     * inside a shell (e.g. bash, cmd, powershell, ...)
     * If omitted `shell` is used.
     */
    type?: 'shell' | 'process';

    /**
     * The command to be executed. Can be an external program or a shell
     * command. Can be omitted.
     */
    command?: string;

    /**
     * The arguments passed to the command. Can be omitted.
     */
    args?: string[];

    /**
     * The task's name.
     * If root label omitted the file name is used.
     */
    label?: string;

    /**
      * The command options used when the command is executed. Can be omitted.
      */
    options?: CommandOptions;

    /**
     * The configuration of the available tasks.
     */
    tasks?: TaskDescription[];
}

/**
 * Options to be passed to the external program or shell
 */
export interface CommandOptions {
    /**
     * The current working directory of the executed program or shell.
     * If omitted try the following valus in turn.
     * - the current notebook's root
     * - the directory of current file
     * - the directory of executing task file
     */
    cwd?: string;

    /**
     * The environment of the executed program or shell. If omitted
     * the parent process' environment is used.
     */
    env?: { [key: string]: string };

    /**
     * Configuration of the shell when task type is `shell`
     */
    shell?: {
        /**
         * The shell to use. 
         * If omitted, the OS-specific shell is used.
         * - `PowerShell.exe` for windows
         * - `/bin/bash` for linux or macOS
         */
        executable: string;

        /**
         * The arguments to be passed to the shell executable to run in command mode.
         * If omitted, the default value is used.
         * - ['/D', '/S', '/C'] for `cmd.exe`
         * - ['-Command'] for `PowerShell.exe`
         * - ['-c'] for `/bin/bash`
         */
        args?: string[];
    };
}
