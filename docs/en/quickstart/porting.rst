Porting Guide
=======

This guide will help you port ctshell to different hardware platforms.

ESP32
-------

Coming soon to the ``idf-registry`` component library.

STM32
-------

Refer to the `stm32 port <https://github.com/MDLZCOOL/ctshell/tree/main/port/stm32>`_.

Generic Porting Guide
-------

1. Prepare Project

Prepare a project capable of sending and receiving data via UART.

2. Add Files

Copy ``ctshell.c`` and ``ctshell.h`` to your project directory and add them to your build system.

3. Implement IO Interface

You need to define and populate the ``ctshell_io_t`` structure.

*   write: Serial transmission function. It should be a blocking send or ensure the data is copied to the transmission buffer.
*   get_tick: (Optional) Retrieves the system timestamp in milliseconds, used for ``ctshell_delay``. If there is no system clock, you can set this to NULL, but the delay function in Shell scripts will be unavailable.

Taking STM32 HAL as an example:

.. code-block:: c

    static void stm32_shell_write(const char *str, uint16_t len, void *p) {
        ctshell_stm32_priv_t *d = (ctshell_stm32_priv_t *) p;
        HAL_UART_Transmit(d->huart, (uint8_t *) str, len, 100);
    }

    ctshell_io_t io = {
        .write = stm32_shell_write,
        .get_tick = HAL_GetTick,
    };

4. Handle Input

Ctshell requires manually feeding received bytes. Pass individual bytes into ``ctshell_input``.

STM32 Interrupt Reception Example:

.. code-block:: c

    void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
        if (huart == &huart1) {
            ctshell_input(&ctx, rx_byte);
            HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
        }
    }

5. Initialization and Main Loop

Bare Metal Environment: Initialize the Shell after initializing the UART in the `main()` function, then poll it in the main loop.

.. code-block:: c

    int main(void) {
        HAL_Init();
        MX_USART1_UART_Init();

        ctshell_stm32_init(&ctx, &huart1);

        while (1) {
            ctshell_poll(&ctx);
        }
    }

RTOS Environment: It is recommended to create a dedicated task to run the Shell.

.. code-block:: c

    void shell_task(void *argument) {
        ctshell_stm32_init(&ctx, &huart1);

        while (1) {
            ctshell_poll(&ctx);
            osDelay(10);
        }
    }

6. Testing

Connect via serial terminal software (e.g., MobaXterm, SecureCRT, Putty). Type ``help`` and press Enter. If you see the command list, the porting was successful.
