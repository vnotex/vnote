interface TaskConfiguration {
    /**
     * The configuration's version number
     * If omitted latest version is used.
     */
    version?: '0.1.3';

    /**
     * Windows specific task configuration
     */
    windows?: TaskConfiguration;

    /**
     * macOS specific task configuration
     */
    osx?: TaskConfiguration;

    /**
     * Linux specific task configuration
     */
    linux?: TaskConfiguration;

    /**
     * The type of a custom task. Tasks of type "shell" are executed
     * inside a shell (e.g. bash, cmd, powershell, ...)
     * If omitted, the parent type is used
     * If no parent specific, the `shell` is used.
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
     * If task has no parent, the file name is used.
     * If task has command, the command is used.
     */
    label?: string | TranslatableString;

    /**
      * The command options used when the command is executed. Can be omitted.
      */
    options?: CommandOptions;

    /**
     * The configuration of the available tasks.
     * Tasks will not be inherited.
     * Tasks in OS-specific will be merged.
     */
    tasks?: TaskConfiguration[];
}

/**
 * Options to be passed to the external program or shell
 */
interface CommandOptions {
    /**
     * The current working directory of the executed program or shell.
     * If omitted try the following valus in turn.
     * - the parent task working dir
     * - the current notebook's root
     * - the directory of current file
     * - the directory of executing task file
     */
    cwd?: string;

    /**
     * The environment of the executed program or shell.
     * If omitted the parent process' environment is used.
     */
    env?: { [key: string]: string };

    /**
     * Configuration of the shell when task type is `shell`
     */
    shell?: {
        /**
         * The shell to use. 
         * If omitted, the parent shell is used
         * If no parent specific, the OS-specific shell is used.
         * - `PowerShell.exe` for windows
         * - `/bin/bash` for linux or macOS
         */
        executable: string;

        /**
         * The arguments to be passed to the shell executable to run in command mode.
         * If omitted, the parent shell is used
         * If no parent specific, the default value is used.
         * - ['/D', '/S', '/C'] for `cmd.exe`
         * - ['-Command'] for `PowerShell.exe`
         * - ['-c'] for `/bin/bash`
         */
        args?: string[];
    };
}

/**
 * Localization
 */
interface TranslatableString {
    en_US?: string,
    zh_CN?: string
}