移植指南
=======

本指南将帮助你将 ctsehll 移植到不同的硬件平台。

ESP32
-------

即将上线 ``idf-registry`` 组件库。

STM32
-------

参考 `stm32 port <https://github.com/MDLZCOOL/ctshell/tree/main/port/stm32>`_。

芯片通用移植指南
-------

1. 准备工程

准备一个可以正常使用 uart 发送和接收数据的工程。

2. 添加文件

将下载的仓库复制到你的工程目录，并添加到编译构建系统中，推荐使用CMake，直接set需要的功能，并引入仓库中的CMakeLists.txt，添加${ctshell_srcs}、${ctshell_incs}、${CTSHELL_DEFINITIONS}即可。

3. 实现 IO 接口

你需要定义并填充 ``ctshell_io_t`` 结构体。

*   write：串口发送函数。应当是阻塞发送，或者确保数据被拷贝到发送缓冲区。
*   get_tick：（可选的） 获取系统毫秒级时间戳，用于 ``ctshell_delay``。如果没有系统时钟，可以填 NULL，但在 Shell 脚本中延时功能将不可用。

以 stm32 hal 为例：

.. code-block:: c

    static void stm32_shell_write(const char *str, uint16_t len, void *p) {
        ctshell_stm32_priv_t *d = (ctshell_stm32_priv_t *) p;
        HAL_UART_Transmit(d->huart, (uint8_t *) str, len, 100);
    }

    ctshell_io_t io = {
        .write = stm32_shell_write,
        .get_tick = HAL_GetTick,
    };

4. 处理输入

Ctshell 需要手动送入接收到的字节，将单个字节送入 ``ctshell_input``。

stm32 使用中断接收示例：

.. code-block:: c

    void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
        if (huart == &huart1) {
            ctshell_input(&ctx, rx_byte);
            HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
        }
    }

5. 初始化与主循环

裸机环境: 在 `main()` 函数初始化 uart 后进行 Shell 初始化，并在主循环中轮询。

.. code-block:: c

    int main(void) {
        HAL_Init();
        MX_USART1_UART_Init();

        ctshell_stm32_init(&ctx, &huart1);

        while (1) {
            ctshell_poll(&ctx);
        }
    }

RTOS 环境: 建议创建一个独立的任务来运行 Shell。

.. code-block:: c

    void shell_task(void *argument) {
        ctshell_stm32_init(&ctx, &huart1);

        while (1) {
            ctshell_poll(&ctx);
            osDelay(10);
        }
    }

6. 链接脚本修改

    gcc：无需修改。

    MDK-ARM（v5 and v6）：在scatter file中添加示例如下：

.. code-block:: c

      CtshellCmdSection +0 {
        *(ctshell_cmd_section)
      }

7. 测试

连接串口终端软件（如 MobaXterm, SecureCRT, Putty）。输入 ``help`` 并回车，如果看到命令列表，说明移植成功。
