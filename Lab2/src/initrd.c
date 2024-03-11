#include "initrd.h"
#include "string.h"
#include "mini_uart.h"

static void *archive_start = (void *)0x08000000;

void initrd_list(){
  char *current = (char*)archive_start;
  cpio_header* head;
  
  while(!memcmp(current,"070701",6) && memcmp(current+sizeof(cpio_header),"TRAILER!!!",10)){
    char filename[256];
    head = (cpio_header*)current;
    int filesize = hstr2int(head->c_filesize,8);
    int namesize = hstr2int(head->c_namesize,8);
    // uart_sends("hi\n");
    int padding = (4 - ((filesize+namesize+sizeof(cpio_header))%4))%4;
    
    for(int i = 0; i < namesize; i++){
      filename[i] = *(current+sizeof(cpio_header)+i);
    }
    filename[namesize] = '\0';
    uart_sends(filename);
    uart_sendc('\n');

    current += (filesize+namesize+sizeof(cpio_header)+padding);
  }

}

void initrd_cat(){
  uart_sends("Filename: ");
  char filename[256];
  int i = 0;
  while(1){
    char c = uart_recv();
    uart_sendc(c);
    if(c == '\n'){
      filename[i] = '\0';
      break;
    }
    filename[i++] = c;
  }

  char *current = (char*)archive_start;
  cpio_header* head;
  
  while(!memcmp(current,"070701",6) && memcmp(current+sizeof(cpio_header),"TRAILER!!!",10)){
    head = (cpio_header*)current;
    int filesize = hstr2int(head->c_filesize,8);
    int namesize = hstr2int(head->c_namesize,8);
    int n_padding = (4 - ((namesize+sizeof(cpio_header))%4))%4;
    int f_padding = (4 - filesize%4)%4;

    if(!memcmp(filename, current+sizeof(cpio_header),strlen(filename))){
      for(int i = 0; i < filesize; i++)
        uart_sendc(*(current+sizeof(cpio_header)+namesize+n_padding+i));
      uart_sendc('\n');
      return;
    }
    current += (filesize+namesize+sizeof(cpio_header)+n_padding+f_padding);
  }
  uart_sends("cat: ");
  uart_sends(filename);
  uart_sends(" No such file or directory\n");
}