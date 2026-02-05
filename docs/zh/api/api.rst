API 参考手册
=======

概述
-------
Ctshell 是一个轻量级的嵌入式 Shell 库，旨在提供类似于 Linux 终端的交互体验。它支持历史记录、Tab 键自动补全、环境变量、参数解析以及 Ctrl+C 中断处理。

本文档详细描述了 ctshell 的数据结构、宏定义及 API 接口。

配置宏
-------

这些宏定义在 ``ctshell.h`` 中，用户可根据硬件资源和需求进行修改。

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

数据结构
-------

ctshell_io_t
^^^^^^^
Shell 的底层输入输出接口结构体。在初始化时必须提供。

.. code-block:: c

    typedef struct {
        // 输出函数：发送数据到串口
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

命令注册 API
-------

Ctshell 使用链接器段（Section）自动收集命令。无需手动维护命令数组。

CTSHELL_EXPORT_CMD
^^^^^^^
注册一个 Shell 命令。

.. code-block:: c

    #define CTSHELL_EXPORT_CMD(_name, _func, _desc)

:参数:
    * ``_name``: 命令名称（不带引号的符号，例如 ``help``）。
    * ``_func``: 命令回调函数，类型为 ``int func(int argc, char *argv[])``。
    * ``_desc``: 命令描述字符串。

:示例:

    .. code-block:: c

        int cmd_hello(int argc, char *argv[]) {
            ctshell_printf("Hello World!\r\n");
            return 0;
        }
        CTSHELL_EXPORT_CMD(hello, cmd_hello, "Print hello message");

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

Ctshell 默认提供以下命令：

1. **help**: 列出所有可用命令及其描述。
2. **clear**: 清屏（发送 ANSI 清屏序列）。
3. **echo**: 回显输入的参数。
4. **set**: 设置或显示环境变量。
    * 用法: ``set`` (显示所有环境变量)
    * 用法: ``set [NAME] [VALUE]``
5. **unset**: 删除环境变量。
    * 用法: ``unset [NAME]``

环境变量特性
-------

* 在命令行中输入 ``$VAR_NAME``，Shell 会自动展开为对应的值。
* 变量名最大长度由 ``CTSHELL_VAR_NAME_LEN`` 决定。
* 变量值最大长度由 ``CTSHELL_VAR_VAL_LEN`` 决定。
