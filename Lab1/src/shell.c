#include "shell.h"
#include "mini_uart.h"
#include "command.h"
#include "string.h"

#define BUF_LEN 256

void shell_exec(){
  char buffer[BUF_LEN + 1];
  char *tmp;

  while (1){
    uart_writeS("# ");
    tmp = buffer;
    for (int i = 0; i < BUF_LEN; i++)
    {
      *tmp = uart_read();
      uart_write(*tmp);
      if (*tmp == 127){
        i--;
        if(*tmp != buffer[0]){
          *tmp-- = 0;          
          uart_write('\b');
          uart_write(' ');
          uart_write('\b'); 
        }
      }
      else if (*tmp == '\n'){
        *tmp = '\0';
        break;
      }
      else tmp++;
    }
    command_exec(buffer);
  }
}

void command_exec(const char *s){
  for (int i = 0; i < MAX_COMM_NUM; i++)
  {
    if (!strcmp(s, commands[i].name)){
      commands[i].func();
      return;
    }
  }
  uart_writeS(s);
  uart_writeS(": command not found\n");
}