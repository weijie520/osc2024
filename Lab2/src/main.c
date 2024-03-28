#include "mini_uart.h"
#include "shell.h"
#include "string.h"
#include "heap.h"
#include "devicetree.h"
#include "initrd.h"


void main(void* dtb_ptr){
    uart_init();

    uart_sends("\nDtb loaded at ");
    uart_sendh((uint64_t)dtb_ptr);

    uart_sends("\nbefore: ");
    uart_sendh(get_initrd());
    fdt_traverse(dtb_ptr, initramfs_callback);
    uart_sends("\nafter: ");
    uart_sendh(get_initrd());
    uart_sends("\n");

    uart_sends("simple malloc test:\n");
    char *str = (char*)simple_malloc(7);
    char cpy[] = "Hello";
    strcpy(str, cpy);
    uart_sends("The malloc str: ");
    uart_sends(str);
    uart_sends("\n");

    shell_exec();
}