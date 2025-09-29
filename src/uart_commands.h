#ifndef UART_COMMANDS_H
#define UART_COMMANDS_H

#include <zephyr/kernel.h>

/* UART command functions */
int uart_commands_init(void);
void uart_commands_start(void);

#endif /* UART_COMMANDS_H */