#include "uart_commands.h"
#include "ble_scanner.h"
#include <zephyr/logging/log.h>
#include <zephyr/console/console.h>
#include <string.h>

LOG_MODULE_REGISTER(uart_commands, LOG_LEVEL_INF);

#define COMMAND_BUFFER_SIZE 64

static void process_command(const char *cmd)
{
    if (strlen(cmd) == 0) {
        return;
    }

    LOG_INF("Processing command: '%s'", cmd);

    switch (cmd[0]) {
        case '?':
            printDiscoveredHIs();
            break;

        default:
            LOG_INF("Unknown command: '%c'", cmd[0]);
            LOG_INF("Available commands:");
            LOG_INF("  ?: Print discovered HIs");
            break;
    }
}

static void uart_command_thread(void)
{
    char command_buffer[COMMAND_BUFFER_SIZE];

    LOG_INF("UART command interface ready");

    while (1) {
        char *line = console_getline();
        if (line != NULL) {
            // Remove trailing newline if present
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }

            if (strlen(line) > 0) {
                strncpy(command_buffer, line, COMMAND_BUFFER_SIZE - 1);
                command_buffer[COMMAND_BUFFER_SIZE - 1] = '\0';
                process_command(command_buffer);
            }
        }
        k_msleep(10);
    }
}

K_THREAD_DEFINE(uart_cmd_thread, 1024, uart_command_thread, NULL, NULL, NULL, 7, 0, 0);

int uart_commands_init(void)
{
    console_init();
    LOG_INF("UART commands initialized");
    return 0;
}

void uart_commands_start(void)
{
    LOG_INF("UART command interface started");
}