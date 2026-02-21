/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "ctshell.h"

static void dummy_shell_write(const char *str, uint16_t len, void *p) {
    CTSHELL_UNUSED_PARAM(str);
    CTSHELL_UNUSED_PARAM(len);
    CTSHELL_UNUSED_PARAM(p);
}

static uint32_t dummy_get_tick(void) {
    static uint32_t tick = 0;
    return tick++;
}

static int cmd_fuzz_complex(int argc, char *argv[]) {
    ctshell_arg_parser_t parser;
    ctshell_args_init(&parser, argc, argv);
    ctshell_expect_int(&parser, "-i", "--int");
    ctshell_expect_str(&parser, "-s", "--str");
    ctshell_expect_bool(&parser, "-v", "--verbose");
    ctshell_expect_verb(&parser, "start");
    ctshell_expect_verb(&parser, "stop");
#ifdef CONFIG_CTSHELL_USE_DOUBLE
    ctshell_expect_double(&parser, "-d", "--double");
#endif
    ctshell_args_parse(&parser);
    int i_val = ctshell_get_int(&parser, "-i");
    char *s_val = ctshell_get_str(&parser, "-s");
    int b_val = ctshell_get_bool(&parser, "-v");

    CTSHELL_UNUSED_PARAM(i_val);
    CTSHELL_UNUSED_PARAM(s_val);
    CTSHELL_UNUSED_PARAM(b_val);

    return 0;
}
CTSHELL_EXPORT_CMD(fuzz, cmd_fuzz_complex, "Fuzz complex args parsing", CTSHELL_ATTR_NONE);

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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) {
        return 0;
    }

    ctshell_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctshell_io_t io = {
            .write = dummy_shell_write,
            .get_tick = dummy_get_tick,
    };
    ctshell_init(&ctx, io, NULL);
    for (size_t i = 0; i < size; i++) {
        ctshell_input(&ctx, (char)data[i]);
        ctshell_poll(&ctx);
    }
    ctshell_poll(&ctx);
    return 0;
}
