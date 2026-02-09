API Reference
=======

Overview
-------
Ctshell is a lightweight embedded shell library designed to provide an interactive experience similar to a Linux terminal. It supports command history, tab completion, environment variables, parameter parsing, and Ctrl+C interrupt handling.

This document provides a detailed description of ctshell data structures, macro definitions, and API interfaces.

Configuration macros
-------

These macro definitions are located in ``ctshell_config.h``, and users can modify them according to their hardware resources and requirements.

.. list-table::
   :widths: 30 15 55
   :header-rows: 1

   * - Macro Name
     - Default
     - Description
   * - ``CTSHELL_CMD_NAME_MAX_LEN``
     - 16
     - The maximum length of a command name.
   * - ``CTSHELL_LINE_BUF_SIZE``
     - 128
     - The maximum length of the command-line input buffer.
   * - ``CTSHELL_MAX_ARGS``
     - 16
     - The maximum number of parameters supported by a single command.
   * - ``CTSHELL_HISTORY_SIZE``
     - 5
     - The number of historical command entries recorded.
   * - ``CTSHELL_VAR_MAX_COUNT``
     - 8
     - The maximum number of environment variables.
   * - ``CTSHELL_VAR_NAME_LEN``
     - 16
     - The maximum length of an environment variable name.
   * - ``CTSHELL_VAR_VAL_LEN``
     - 32
     - The maximum length of environment variable values.
   * - ``CTSHELL_FIFO_SIZE``
     - 128
     - Enter the input FIFO buffer size.
   * - ``CTSHELL_PROMPT``
     - "ctsh>> "
     - The default prompt of the shell.
   * - ``CTSHELL_USE_DOUBLE``
     - Undefined
     - If this macro is defined, support for parsing floating-point parameters will be enabled.
   * - ``CTSHELL_FS_PATH_MAX``
     - 128
     - The maximum length of a file system path.
   * - ``CTSHELL_FS_NAME_MAX``
     - 16
     - The maximum length of file system filenames.
   * - ``CTSHELL_USE_FS``
     - Undefined
     - If this macro is defined, file system support will be enabled.
   * - ``CTSHELL_USE_FS_FATFS``
     - Undefined
     - If this macro is defined, support for the FATFS file system will be enabled. This will only work if ``CTSHELL_USE_FS`` is also enabled.
   * - ``CTSHELL_USE_BUILTIN_CMDS``
     - On by default
     - If this macro is defined, support for built-in commands will be enabled.

Data Structures
-------

ctshell_io_t
^^^^^^^
The underlying input/output interface structure for the shell. This must be provided during initialization.

.. code-block:: c

    typedef struct {
        // Output function, sends data
        void (*write)(const char *str, uint16_t len, void *priv);
        // Time acquisition function: obtains the system tick count in milliseconds
        uint32_t (*get_tick)(void);
    } ctshell_io_t;

ctshell_ctx_t
^^^^^^^
The main context structure of the shell. It contains all the runtime state.

.. note::
    The user should allocate memory for this structure, typically as a static global variable, and pass its pointer to the API. The user should not directly access the internal members of the structure.

Core API
-------

ctshell_init
^^^^^^^
Initialize Shell context.

.. code-block:: c

    void ctshell_init(ctshell_ctx_t *ctx, ctshell_io_t io, void *priv);

:Parameters:
    * ``ctx``: A pointer to the Shell context.
    * ``io``: An IO structure containing underlying write and time functions.
    * ``priv``: A pointer to user-private data, which will be passed back when ``io.write`` is called.

ctshell_input
^^^^^^^
The input processing function reads the input character by character.

.. code-block:: c

    void ctshell_input(ctshell_ctx_t *ctx, char byte);

:Parameters:
    * ``ctx``: A pointer to the Shell context.
    * ``byte``: The single character received.

:Description:
    This function stores the received characters into a FIFO buffer without blocking. It also handles the ``Ctrl+C`` interrupt signal.

ctshell_poll
^^^^^^^
The main loop polling function for the shell. It needs to be called within the main loop.

.. code-block:: c

    void ctshell_poll(ctshell_ctx_t *ctx);

:Parameters:
    * ``ctx``: A pointer to the Shell context.

:Description:
    This function retrieves data from the FIFO buffer, parses ANSI escape sequences, and handles line editing logic.

Tool API
-------

ctshell_printf
^^^^^^^
Formatted output function, similar to the standard printf.

.. code-block:: c

    void ctshell_printf(const char *fmt, ...);

:Parameters:
    * ``fmt``: Formatted string.
    * ``...``: Variable parameters.

:Note:
    This function internally depends on the global context pointer ``g_ctshell_ctx``, therefore it must be called after ``ctshell_init``.

ctshell_error
^^^^^^^
A macro that outputs error messages in the format ``Error: <message>\r\n``.

.. code-block:: c

    #define ctshell_error(fmt, ...)

CTSHELL_UNUSED_PARAM
^^^^^^^
A macro to suppress warnings about unused variables.

.. code-block:: c

    #define CTSHELL_UNUSED_PARAM(x)

ctshell_delay
^^^^^^^
Delay function with interrupt detection.

.. code-block:: c

    void ctshell_delay(ctshell_ctx_t *ctx, uint32_t ms);

:Parameters:
    * ``ctx``: A pointer to the Shell context.
    * ``ms``: Delay in milliseconds.

:Description:
    During the delay period, if the user presses ``Ctrl+C``, the function will exit the current command execution via ``longjmp``. The ``get_tick`` function in ``ctshell_io_t`` must be implemented; otherwise, this functionality will not work.

ctshell_check_abort
^^^^^^^
Check if a termination signal (Ctrl+C) has been received.

.. code-block:: c

    void ctshell_check_abort(ctshell_ctx_t *ctx);

:Description:
    In long-running command loops, this function should be called manually in response to a user's termination request.

Filesystem API
-------

ctshell_fatfs_init
^^^^^^
Initialize ctshell support for the FatFS file system. This should be called after ctshell initialization.

.. code-block:: c

    void ctshell_fatfs_init(ctshell_ctx_t *ctx, ctshell_fs_t *fs);

:Parameters:
    * ``ctx``: A pointer to the Shell context.
    * ``fs``: A pointer to the file system interface structure.

Command Register API
-------

Ctshell automatically collects commands using linker sections. There is no need to manually maintain a command array.

Currently, the following attributes (_attr) are available:

CTSHELL_ATTR_NONE: Indicates that the command is a regular command.

CTSHELL_ATTR_MENU: Indicates that the command is a menu command containing subcommands.

CTSHELL_ATTR_HIDDEN: Indicates that the command will not be displayed in the ``help`` command output.

CTSHELL_EXPORT_CMD
^^^^^^^
Register a command.

.. code-block:: c

    #define CTSHELL_EXPORT_CMD(_name, _func, _desc, _attr)

:Parameters:
    * ``_name``: Command name (a symbol without quotes, e.g., ``help``).
    * ``_func``: Command callback function, of type ``int func(int argc, char *argv[])``.
    * ``_desc``: Command description string.
    * ``_attr``: Command attributes.

:Example:

    .. code-block:: c

        int cmd_hello(int argc, char *argv[]) {
            ctshell_printf("Hello World!\r\n");
            return 0;
        }
        CTSHELL_EXPORT_CMD(hello, cmd_hello, "Print hello message", CTSHELL_CMD_ATTR_NONE);

CTSHELL_EXPORT_CMD
^^^^^^^
Register a subcommand and attach it under an existing parent command.

.. code-block:: c

    CTSHELL_EXPORT_SUBCMD(_parent, _name, _func, _desc)

:Parameters:
    * ``_parent``: The name of the parent command, which must have already been registered via CTSHELL_EXPORT_CMD.
    * ``_name``: The name of the subcommand.
    * ``_func``: The subcommand callback function.
    * ``_desc``: The subcommand description string.

:Note:
    Subcommands inherit the CTSHELL_ATTR_NONE attribute by default.

:Example:
    .. code-block:: c

        /*
         * Register root menu: "net"
         * func=NULL, attr=MENU, This is a pure container.
         */
        CTSHELL_EXPORT_CMD(net, NULL, "Network tools", CTSHELL_ATTR_MENU);

        /*
         * Registration command for the second level: "net ip"
         * parent="net", mounted under "net"
         * This is a leaf node; entering "net ip" will execute the command directly.
         */
        int cmd_net_ip(int argc, char *argv[]) {
            ctshell_printf("IP Address : 192.168.1.100\r\n");
            ctshell_printf("Subnet Mask: 255.255.255.0\r\n");
            return 0;
        }
        CTSHELL_EXPORT_SUBCMD(net, ip, cmd_net_ip, "Show IP address");

        /*
         * Registration container for the second level: "net wifi"
         * parent="net"
         * func=NULL. This is a pure container.
         */
        CTSHELL_EXPORT_SUBCMD(net, wifi, NULL, "WiFi management");

        /*
         * Registration command for the third level: "net wifi connect"
         * parent="net_wifi". Note: The parent node name is a concatenation of the first two level names (net + _ + wifi)
         */
        int cmd_wifi_connect(int argc, char *argv[]) {
            ctshell_arg_parser_t parser;
            ctshell_args_init(&parser, argc, argv);

            // Define expected parameters: -s <ssid> and -p <password>
            ctshell_expect_str(&parser, "-s", "ssid");
            ctshell_expect_str(&parser, "-p", "password");

            ctshell_args_parse(&parser);

            if (ctshell_has(&parser, "ssid") && ctshell_has(&parser, "password")) {
                char *ssid = ctshell_get_str(&parser, "ssid");
                char *pwd  = ctshell_get_str(&parser, "password");

                ctshell_printf("Connecting to %s (Key: %s)...\r\n", ssid, pwd);
            } else {
                ctshell_printf("Usage: net wifi connect -s <ssid> -p <password>\r\n");
            }
            return 0;
        }
        CTSHELL_EXPORT_SUBCMD(net_wifi, connect, cmd_wifi_connect, "Connect to AP");


Parameter Parser API
-------

Ctshell includes a lightweight parameter parser for handling command-line arguments.

Structures
^^^^^^^
* ``ctshell_arg_parser_t``: Parser context object.

Usage Flow
^^^^^^^
1. ``ctshell_args_init``: Initialization.
2. ``ctshell_expect_*``: Configure expected parameters.
3. ``ctshell_args_parse``: Perform parsing.
4. ``ctshell_get_*``: Retrieve parsing results.

API List
^^^^^^^

.. code-block:: c

    // Initialize the parser
    void ctshell_args_init(ctshell_arg_parser_t *parser, int argc, char *argv[]);

    // Configure expected parameters
    // flag: Flag in the command line (e.g., "-n")
    // key: Key name used to retrieve the value (e.g., "count"). If NULL, flag is used as the key
    void ctshell_expect_int(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_str(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_bool(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_verb(ctshell_arg_parser_t *p, const char *verb_name);

    #ifdef CTSHELL_USE_DOUBLE
    void ctshell_expect_double(ctshell_arg_parser_t *p, const char *flag, const char *key);
    #endif

    // Perform parsing
    void ctshell_args_parse(ctshell_arg_parser_t *p);

    // Retrieve results
    // Returns 0, NULL, or 0.0 if the parameter is not found
    int ctshell_get_int(ctshell_arg_parser_t *p, const char *key);
    char *ctshell_get_str(ctshell_arg_parser_t *p, const char *key);
    int ctshell_get_bool(ctshell_arg_parser_t *p, const char *key);
    
    #ifdef CTSHELL_USE_DOUBLE
    double ctshell_get_double(ctshell_arg_parser_t *p, const char *key);
    #endif

    // Check whether a parameter exists
    int ctshell_has(ctshell_arg_parser_t *p, const char *key);

Parameter Parsing Example
^^^^^^^

.. code-block:: c

    // Command: test -i 100 -s "hello" -b
    int cmd_test(int argc, char *argv[]) {
        ctshell_arg_parser_t parser;
        ctshell_args_init(&parser, argc, argv);
        
        // Define expected parameters
        ctshell_expect_int(&parser, "-i", "count");
        ctshell_expect_str(&parser, "-s", "message");
        ctshell_expect_bool(&parser, "-b", "flag");
        
        // Parse
        ctshell_args_parse(&parser);
        
        // Retrieve values
        if (ctshell_has(&parser, "count")) {
            int val = ctshell_get_int(&parser, "count");
            ctshell_printf("Count: %d\r\n", val);
        }
        
        if (ctshell_get_bool(&parser, "flag")) {
            ctshell_printf("Flag is set\r\n");
        }
        
        return 0;
    }

Built-in Commands
-------

Ctshell provides the following built-in commands:

1. **help**: Lists all available commands and their descriptions.
    * Note: Use ``help + MENU`` to view commands under that MENU.
2. **clear**: Clears the screen (sends ANSI clear screen sequence).
3. **echo**: Echoes input parameters. If file system support is enabled, it can also write to files, supporting overwrite ``>`` and append ``>>``.
4. **set**: Set or display environment variables.
    * Usage: ``set`` (display all environment variables)
    * Usage: ``set [NAME] [VALUE]``
5. **unset**: Delete an environment variable.
    * Usage: ``unset [NAME]``

If file system support is enabled, the following built-in commands are available:

6. **cd**: Change the working directory.
7. **pwd**: Display the absolute path of the current working directory.
8. **ls**: List files and directories in the current directory, including file sizes.
9. **cat**: Display file contents.
10. **mkdir**: Create a directory.
11. **rm**: Delete a file or directory.
12. **touch**: Create an empty file.

Environment Variable Features
-------

* Enter ``$VAR_NAME`` in the command line, and the Shell will automatically expand it to the corresponding value.
* The maximum variable name length is determined by ``CTSHELL_VAR_NAME_LEN``.
* The maximum variable value length is determined by ``CTSHELL_VAR_VAL_LEN``.
