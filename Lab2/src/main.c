#include "mini_uart.h"
#include "shell.h"
#include "string.h"
#include "heap.h"

void main(){
    uart_init();
    uart_sends("Hello world\n");

    shell_exec();
}