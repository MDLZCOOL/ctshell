/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <esp_log.h>
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "ctshell_esp32.h"

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

void app_main(void) {
    ctshell_port_esp32_init();

    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
