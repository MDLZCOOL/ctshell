ctshell
=======

.. image:: https://img.shields.io/github/license/MDLZCOOL/ctshell
   :target: https://github.com/MDLZCOOL/ctshell/blob/main/LICENSE
   :alt: license

Ctshell 是一款小而美的、低开销的shell，专为资源受限的嵌入式系统而设计。

特性
-------

* 命令补全：支持使用 TAB 键自动补全命令。
* 命令历史记录：支持使用向上 (↑) 和向下 (↓) 箭头键浏览历史记录。
* 行编辑：支持光标移动（左/右）、退格键处理以及在行内任意位置插入文本。
* 环境变量：支持设置、取消设置、列出变量，并使用“$”前缀进行内联扩展。
* 非阻塞架构：输入和处理过程解耦，使其兼容裸机和实时操作系统环境。
* 信号处理 (SIGINT)：实现 setjmp/longjmp 逻辑，可通过 Ctrl+C 中断长时间运行的命令。
* 内置参数解析器：包含一个强类型参数解析器，可轻松处理自定义命令中的标志（布尔值）、整数、字符串和子命令。
* ANSI 转义序列支持：处理用于箭头键和屏幕控制的标准 VT100/ANSI 转义码。

移植
-------

Ctshell 易于移植——它提供了针对多种主流嵌入式平台的原生移植实现，基本无需额外配置即可使用。

更多详情，请参阅相关文档。

Demo
-------

Ctshell 提供直观的交互体验，其核心功能演示如下：

**查看所有可用命令**

使用 ``help`` 命令可以快速列出所有可用命令及其简要说明。

.. image:: ../assets/help.png

**调试嵌入式系统**

将ctshell集成到您的嵌入式项目中，可以快速调试外设/模块的状态。

.. image:: ../assets/demo_ina226.png

**操作环境变量**

使用 ``set``/``unset`` 命令灵活管理环境变量，并使用 ``$`` 前缀展开变量。

.. image:: ../assets/variables.png

**Abort Long-running Commands**

使用 ``Ctrl+C``（SIGINT）可以立即终止死循环/长时间运行的命令，而不会导致系统崩溃。

.. image:: ../assets/sigint.png

基本用法
-------

本部分仅介绍自定义命令的基本用法（不涉及端口细节）。实现过程只需两个关键步骤：实现自定义命令函数，并使用一个宏导出该命令。Ctshell 会自动处理底层工作，例如命令注册、列表维护和参数解析。

以下将以INA226电源监控器驱动程序的命令开发为例，全面演示如何使用子命令和多种类型参数来实现复杂命令，这是ctshell命令在嵌入式开发中最典型的使用场景。

.. code-block:: c

    #include "ctshell.h"

    int cmd_ina226(int argc, char *argv[]) {
        if (argc < 2) {
            ina226_print_usage();
            return 0;
        }

        // 初始化ctshell参数解析器
        ctshell_arg_parser_t parser;
        ctshell_args_init(&parser, argc, argv);

        // 声明预期的子命令
        ctshell_expect_verb(&parser, "start");
        ctshell_expect_verb(&parser, "stop");
        ctshell_expect_verb(&parser, "status");

        // 声明预期参数，这些参数必须是强类型化的，格式为（解析器、短选项、参数别名）
        // 参数别名用于后续的值检索
        ctshell_expect_int(&parser, "-b", "bus");
        ctshell_expect_int(&parser, "-a", "addr");
        ctshell_expect_int(&parser, "-t", "battery_idx");
        ctshell_expect_bool(&parser, "-f", "keep_running");

        // 执行参数解析，底层会自动验证参数类型和格式，无需手动处理
        ctshell_args_parse(&parser);

        // 根据子命令/参数执行业务逻辑
        // ctshell_has：检查是否传入了指定的子命令/参数
        // ctshell_get_*: 获取参数值
        if (ctshell_has(&parser, "start")) {
            int bus = ctshell_has(&parser, "bus") ? ctshell_get_int(&parser, "bus") : INA226_DEFAULT_BUS;
            int addr = ctshell_has(&parser, "addr") ? ctshell_get_int(&parser, "addr") : INA226_DEFAULT_ADDR;
            int bat_idx = ctshell_has(&parser, "battery_idx") ? ctshell_get_int(&parser, "battery_idx") : 1;
            int force = ctshell_get_bool(&parser, "keep_running");
            return ina226_start(bus, addr, bat_idx, force);
        } else if (ctshell_has(&parser, "stop")) {
            return ina226_stop();
        } else if (ctshell_has(&parser, "status")) {
            return ina226_status();
        } else {
            ina226_print_usage();
        }

        return 0;
    }
    // 使用单个宏导出命令，ctshell 会自动注册，无需手动维护命令列表
    // 宏参数：<命令名称> <命令功能> <命令描述>
    CTSHELL_EXPORT_CMD(ina226, cmd_ina226, "INA226 power monitor driver");

命令注册后，即可在ctshell终端中以任意参数顺序交互式地使用该命令。以下是上面提到的 ``ina226`` 命令的实际调用方法：

.. code-block:: shell

    # 启动 INA226：使用默认的 I2C 总线/地址，电池索引为 2，启用连续运行模式
    ina226 start -t 2 -f

    # 启动 INA226：指定 I2C 总线 1，设备地址为 0x40，其他参数使用默认值
    ina226 start -b 1 -a 64  # 十进制数 64 对应十六进制数 0x40，ctshell 原生支持十进制/十六进制输入。

    # 停止 INA226 数据采集
    ina226 stop

    # 查看 INA226 的当前工作状态
    ina226 status

    # 查看命令用法
    ina226

正如您所见，ctshell 为嵌入式 shell 的使用提供了流畅的交互体验。它为用户提供了直观的调用流程，学习曲线平缓，心智负担较低。

文档
-------

参见 `ctshell Documentation <https://ctshell.readthedocs.io/zh-cn/latest/>`_。

许可
-------

Copyright 2026 MDLZCOOL.

根据 `Apache 2.0 许可证 <https://github.com/MDLZCOOL/ctshell/blob/main/LICENSE>`_ 的条款分发。

.. toctree::
   :maxdepth: 1
   :caption: 快速开始

   quickstart/porting

.. toctree::
   :maxdepth: 1
   :caption: API 参考手册

   api/api