API 参考手册
=======

概述
-------
Ctshell 是一个轻量级的嵌入式 Shell 库，旨在提供类似于 Linux 终端的交互体验。它支持历史记录、Tab 键自动补全、环境变量、参数解析以及 Ctrl+C 中断处理。

本文档详细描述了 ctshell 的数据结构、宏定义及 API 接口。

配置宏
-------

这些宏定义在 ``ctshell_config.h`` 中，用户可根据硬件资源和需求进行修改。

.. list-table::
   :widths: 30 15 55
   :header-rows: 1

   * - 宏名称
     - 默认值
     - 描述
   * - ``CTSHELL_CMD_NAME_MAX_LEN``
     - 16
     - 命令名称的最大长度。
   * - ``CTSHELL_LINE_BUF_SIZE``
     - 128
     - 命令行输入缓冲区的最大长度。
   * - ``CTSHELL_MAX_ARGS``
     - 16
     - 单个命令支持的最大参数数量。
   * - ``CTSHELL_HISTORY_SIZE``
     - 5
     - 历史命令记录的条数。
   * - ``CTSHELL_VAR_MAX_COUNT``
     - 8
     - 环境变量的最大数量。
   * - ``CTSHELL_VAR_NAME_LEN``
     - 16
     - 环境变量名的最大长度。
   * - ``CTSHELL_VAR_VAL_LEN``
     - 32
     - 环境变量值的最大长度。
   * - ``CTSHELL_FIFO_SIZE``
     - 128
     - 输入 FIFO 缓冲区大小。
   * - ``CTSHELL_PROMPT``
     - "ctsh>> "
     - Shell 的默认提示符。
   * - ``CTSHELL_USE_DOUBLE``
     - 未定义
     - 若定义此宏，将开启对浮点数参数解析的支持。
   * - ``CTSHELL_FS_PATH_MAX``
     - 128
     - 文件系统路径的最大长度。
   * - ``CTSHELL_FS_NAME_MAX``
     - 16
     - 文件系统文件名最大长度。
   * - ``CTSHELL_USE_FS``
     - 未定义
     - 若定义此宏，将开启对文件系统支持。
   * - ``CTSHELL_USE_FS_FATFS``
     - 未定义
     - 若定义此宏，将开启对 FATFS 文件系统支持，必须先开启 ``CTSHELL_USE_FS`` 才有用。
   * - ``CTSHELL_USE_BUILTIN_CMDS``
     - 默认开启
     - 若定义此宏，将开启对内置命令支持。

数据结构
-------

ctshell_io_t
^^^^^^^
Shell 的底层输入输出接口结构体。在初始化时必须提供。

.. code-block:: c

    typedef struct {
        // 输出函数，发送数据
        void (*write)(const char *str, uint16_t len, void *priv);
        // 时间获取函数：获取系统 Tick，单位ms
        uint32_t (*get_tick)(void);
    } ctshell_io_t;

ctshell_ctx_t
^^^^^^^
Shell 的主上下文结构体。包含了运行时的所有状态。

.. note::
    用户应申请此结构体的内存，通常为静态全局变量，并将其指针传递给 API。用户不应直接访问结构体内部成员。

核心 API
-------

ctshell_init
^^^^^^^
初始化 Shell 上下文。

.. code-block:: c

    void ctshell_init(ctshell_ctx_t *ctx, ctshell_io_t io, void *priv);

:参数:
    * ``ctx``: 指向 Shell 上下文的指针。
    * ``io``: 包含底层写函数和时间函数的 IO 结构体。
    * ``priv``: 用户私有数据指针，会在调用 ``io.write`` 时透传回去。

ctshell_input
^^^^^^^
输入处理函数，逐字符读入。

.. code-block:: c

    void ctshell_input(ctshell_ctx_t *ctx, char byte);

:参数:
    * ``ctx``: Shell 上下文指针。
    * ``byte``: 接收到的单个字符。

:说明:
    此函数会将接收到的字符存入 FIFO 缓冲区，不会阻塞。它还会处理 ``Ctrl+C`` 的中断信号标记。

ctshell_poll
^^^^^^^
Shell 的主循环轮询函数。需要在主循环中调用。

.. code-block:: c

    void ctshell_poll(ctshell_ctx_t *ctx);

:参数:
    * ``ctx``: Shell 上下文指针。

:说明:
    此函数从 FIFO 中取出数据，解析 ANSI 转义序列，并处理行编辑逻辑。

工具 API
-------

ctshell_printf
^^^^^^^
格式化输出函数，类似标准 printf。

.. code-block:: c

    void ctshell_printf(const char *fmt, ...);

:参数:
    * ``fmt``: 格式化字符串。
    * ``...``: 可变参数。

:注意:
    此函数内部依赖全局上下文指针 ``g_ctshell_ctx``，因此必须在 ``ctshell_init`` 之后调用。

ctshell_error
^^^^^^^
输出错误信息的宏，格式为 ``Error: <信息>\r\n``。

.. code-block:: c

    #define ctshell_error(fmt, ...)

CTSHELL_UNUSED_PARAM
^^^^^^^
避免未使用的变量发出警告的宏。

.. code-block:: c

    #define CTSHELL_UNUSED_PARAM(x)

ctshell_delay
^^^^^^^
带中断检测的延时函数。

.. code-block:: c

    void ctshell_delay(ctshell_ctx_t *ctx, uint32_t ms);

:参数:
    * ``ctx``: Shell 上下文指针。
    * ``ms``: 延时毫秒数。

:说明:
    在延时期间，如果用户按下了 ``Ctrl+C``，该函数会通过 ``longjmp`` 跳出当前命令执行。必须实现 ``ctshell_io_t`` 中的 ``get_tick``，否则无效。

ctshell_check_abort
^^^^^^^
检查是否收到终止信号（Ctrl+C）。

.. code-block:: c

    void ctshell_check_abort(ctshell_ctx_t *ctx);

:说明:
    在长时间运行的命令循环中，应手动调用此函数以响应用户的终止请求。

文件系统 API
-------

ctshell_fatfs_init
^^^^^^
初始化 ctshell 对 ``FatFS`` 文件系统的支持。应当在ctshell初始化完成之后调用。

.. code-block:: c

    void ctshell_fatfs_init(ctshell_ctx_t *ctx, ctshell_fs_t *fs);

:参数:
    * ``ctx``: Shell 上下文指针。
    * ``fs``: 文件系统接口结构体指针。

命令注册 API
-------

Ctshell 使用链接器段（Section）自动收集命令。无需手动维护命令数组。

目前有下面几种属性(_attr)：

CTSHELL_ATTR_NONE 表示该命令是一个普通的命令。

CTSHELL_ATTR_MENU 表示该命令是一个含有子命令的菜单命令。

CTSHELL_ATTR_HIDDEN 表示该命令不显示在 ``help`` 命令中。

CTSHELL_EXPORT_CMD
^^^^^^^
注册一个 Shell 命令。

.. code-block:: c

    #define CTSHELL_EXPORT_CMD(_name, _func, _desc, _attr)

:参数:
    * ``_name``: 命令名称（不带引号的符号，例如 ``help``）。
    * ``_func``: 命令回调函数，类型为 ``int func(int argc, char *argv[])``。
    * ``_desc``: 命令描述字符串。
    * ``_attr``: 命令属性。

:示例:

    .. code-block:: c

        int cmd_hello(int argc, char *argv[]) {
            ctshell_printf("Hello World!\r\n");
            return 0;
        }
        CTSHELL_EXPORT_CMD(hello, cmd_hello, "Print hello message", CTSHELL_CMD_ATTR_NONE);

CTSHELL_EXPORT_CMD
^^^^^^^
注册一个子命令，挂载在已有的父命令下。

.. code-block:: c

    CTSHELL_EXPORT_SUBCMD(_parent, _name, _func, _desc)

:参数:
    * ``_parent``: 父命令的名称，必须已通过 CTSHELL_EXPORT_CMD 注册。
    * ``_name``: 子命令名称。
    * ``_func``: 子命令回调函数。
    * ``_desc``: 子命令描述字符串。

:注意:
    子命令默认继承 CTSHELL_ATTR_NONE 属性。

:示例:
    .. code-block:: c

        /*
         * 注册根菜单: "net"
         * func=NULL, attr=MENU, 这是一个纯容器
         */
        CTSHELL_EXPORT_CMD(net, NULL, "Network tools", CTSHELL_ATTR_MENU);

        /*
         * 注册二级具体命令: "net ip"
         * parent="net", 挂载在 net 下
         * 这是一个叶子节点，输入 "net ip" 直接执行
         */
        int cmd_net_ip(int argc, char *argv[]) {
            ctshell_printf("IP Address : 192.168.1.100\r\n");
            ctshell_printf("Subnet Mask: 255.255.255.0\r\n");
            return 0;
        }
        CTSHELL_EXPORT_SUBCMD(net, ip, cmd_net_ip, "Show IP address");

        /*
         * 注册二级菜单容器: "net wifi"
         * parent="net"
         * func=NULL, 这是一个纯容器
         */
        CTSHELL_EXPORT_SUBCMD(net, wifi, NULL, "WiFi management");

        /*
         * 注册三级具体命令: "net wifi connect"
         * parent="net_wifi", 注意：父节点名是前两级名称的拼接 (net + _ + wifi)
         */
        int cmd_wifi_connect(int argc, char *argv[]) {
            ctshell_arg_parser_t parser;
            ctshell_args_init(&parser, argc, argv);

            // 定义参数: -s <ssid> 和 -p <password>
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


参数解析器 API
-------

Ctshell 内置了一个轻量级的参数解析器，用于处理命令行参数。

结构体
^^^^^^^
* ``ctshell_arg_parser_t``: 解析器上下文对象。

使用流程
^^^^^^^
1. ``ctshell_args_init``: 初始化。
2. ``ctshell_expect_*``: 配置期望接收的参数。
3. ``ctshell_args_parse``: 执行解析。
4. ``ctshell_get_*``: 获取解析结果。

API 列表
^^^^^^^

.. code-block:: c

    // 初始化解析器
    void ctshell_args_init(ctshell_arg_parser_t *parser, int argc, char *argv[]);

    // 配置期望参数
    // flag: 命令行中的标志 (如 "-n")
    // key: 获取值时使用的键名 (如 "count")，若为 NULL 则使用 flag 作为键名
    void ctshell_expect_int(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_str(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_bool(ctshell_arg_parser_t *p, const char *flag, const char *key);
    void ctshell_expect_verb(ctshell_arg_parser_t *p, const char *verb_name);

    #ifdef CTSHELL_USE_DOUBLE
    void ctshell_expect_double(ctshell_arg_parser_t *p, const char *flag, const char *key);
    #endif

    // 执行解析
    void ctshell_args_parse(ctshell_arg_parser_t *p);

    // 获取结果
    // 若未找到参数，则返回 0, NULL 或 0.0
    int ctshell_get_int(ctshell_arg_parser_t *p, const char *key);
    char *ctshell_get_str(ctshell_arg_parser_t *p, const char *key);
    int ctshell_get_bool(ctshell_arg_parser_t *p, const char *key);
    
    #ifdef CTSHELL_USE_DOUBLE
    double ctshell_get_double(ctshell_arg_parser_t *p, const char *key);
    #endif

    // 检查参数是否存在
    int ctshell_has(ctshell_arg_parser_t *p, const char *key);

参数解析示例
^^^^^^^

.. code-block:: c

    // 命令: test -i 100 -s "hello" -b
    int cmd_test(int argc, char *argv[]) {
        ctshell_arg_parser_t parser;
        ctshell_args_init(&parser, argc, argv);
        
        // 定义期望参数
        ctshell_expect_int(&parser, "-i", "count");
        ctshell_expect_str(&parser, "-s", "message");
        ctshell_expect_bool(&parser, "-b", "flag");
        
        // 解析
        ctshell_args_parse(&parser);
        
        // 获取值
        if (ctshell_has(&parser, "count")) {
            int val = ctshell_get_int(&parser, "count");
            ctshell_printf("Count: %d\r\n", val);
        }
        
        if (ctshell_get_bool(&parser, "flag")) {
            ctshell_printf("Flag is set\r\n");
        }
        
        return 0;
    }

内置命令
-------

Ctshell 提供以下内置命令：

1. **help**: 列出所有可用命令及其描述。
    * 注意: 使用 ``help + MENU`` 可以查看该 MENU 下的命令。
2. **clear**: 清屏（发送 ANSI 清屏序列）。
3. **echo**: 回显输入的参数，若开启文件系统支持，也可以写入文件，支持覆盖写入 ``>`` 和追加写入 ``>>``。
4. **set**: 设置或显示环境变量。
    * 用法: ``set`` (显示所有环境变量)
    * 用法: ``set [NAME] [VALUE]``
5. **unset**: 删除环境变量。
    * 用法: ``unset [NAME]``

若开启文件系统支持，则下面内置命令可用：

6. **cd**: 切换工作目录。
7. **pwd**: 显示当前工作目录的绝对路径。
8. **ls**: 列出当前目录下的文件和目录，也列出文件大小。
9. **cat**: 显示文件内容。
10. **mkdir**: 创建目录。
11. **rm**: 删除文件或目录。
12. **touch**: 创建空白文件。

环境变量特性
-------

* 在命令行中输入 ``$VAR_NAME``，Shell 会自动展开为对应的值。
* 变量名最大长度由 ``CTSHELL_VAR_NAME_LEN`` 决定。
* 变量值最大长度由 ``CTSHELL_VAR_VAL_LEN`` 决定。
