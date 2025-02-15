#include "thread.h"
#include "memory.h"
#include "timer.h"
#include "mini_uart.h"
#include "shell.h"
#include "interrupt.h"

static int max_tid = 0;

thread *running_queue = 0;
thread *terminated_queue = 0;

thread *idle_thread = 0;

void enqueue(thread **head, thread *t){
  if(!(*head)){
    t->prev = t;
    t->next = t;
    *head = t;
  }else{
    t->prev = (*head)->prev;
    t->next = *head;
    (*head)->prev->next = t;
    (*head)->prev = t;
  }
}

thread *dequeue(thread **head){
  if(!(*head)) return 0;
  thread *tmp = *head;
  if(tmp->next == tmp){
    *head = 0;
  }else{
    tmp->prev->next = tmp->next;
    tmp->next->prev = tmp->prev;
    *head = tmp->next;
  }
  return tmp;
}

void dequeue_t(thread **head, thread *t){
  if(!(*head)) return;
  if(t->next == t){
    *head = 0;
  }else{
    t->prev->next = t->next;
    t->next->prev = t->prev;
  }
}

thread *dequeue_tid(thread **head, int tid){
  thread *tmp = *head;
  do{
    if(tmp->tid == tid){
      if(tmp->next == tmp){
        *head = 0;
      }else{
        tmp->prev->next = tmp->next;
        tmp->next->prev = tmp->prev;
        *head = tmp->next;
      }
      return tmp;
    }
    tmp = tmp->next;
  }while(tmp != *head);
  uart_sends("No such thread tid: ");
  uart_sendi(tid);
  uart_sends("\n");
  return 0;
}

thread *front(thread *q){
  if(!q) return 0;
  return q;
}

void show_queue(thread *q){
  thread *tmp = q;
  if(!q) return;
  do{
    uart_sendi(tmp->tid);
    uart_sends(" ");
    tmp = tmp->next;
  }while(tmp != q);
  uart_sends("\n");
}

void thread_init(){
  idle_thread = thread_create(idle);
  asm volatile("msr tpidr_el1, %0" ::"r"(idle_thread));
}

thread *thread_create(void (*func)(void)){
  thread *t = (thread *)kmalloc(sizeof(thread));
  t->tid = max_tid++;
  t->state = TASK_RUNNING;
  // TODO: Implement implicit thread exit
  t->regs.lr = (unsigned long)func;
  t->stack = kmalloc(THREAD_STACK_SIZE);
  t->kernel_stack = kmalloc(THREAD_STACK_SIZE);
  t->regs.sp = (unsigned long)((char*)t->stack + 0x1000);
  t->regs.fp = t->regs.sp;

  // signal
  for(int i = 0; i < MAX_SIGNAL+1; i++){
    t->signal_handler[i] = 0;
  }
  t->signal_processing = 0;
  t->signal_pending = 0;

  enqueue(&running_queue, t);
  return t;
}

// TODO: Implement implicit thread exit
// void thread_wrapper(void (*func)()){
//   func();
//   thread_exit();
// }

thread *get_thread(int tid){
  thread *tmp = running_queue;
  do{
    if(tmp->tid == tid){
      return tmp;
    }
    tmp = tmp->next;
  }while(tmp != running_queue);
  return 0;
}

void schedule(){
  thread *current = get_current();
  if(current->state != TASK_RUNNING){
    switch_to(current, front(running_queue));
  }
  else{
    thread *next = current->next;
    switch_to(current, next);
  }
}


void idle(){
  while(1){
    kill_zombies();
    schedule();
  }
}

void kill_zombies(){
  while(terminated_queue){
    thread *t = dequeue(&terminated_queue);
    kfree(t->stack);
    kfree(t->kernel_stack);
    kfree(t);
  }
}

void thread_exit(){
  thread *current = get_current();
  current->state = TASK_ZOMBIE;
  running_queue = current->next; // rearrange the queue
  dequeue_t(&running_queue, current);
  enqueue(&terminated_queue, current);
  schedule();
}

void thread_kill(int tid){
  thread *t = get_thread(tid);
  if(!t) return;
  t->state = TASK_ZOMBIE;
  running_queue = t->next; // rearrange the queue
  dequeue_t(&running_queue, t);
  enqueue(&terminated_queue, t);
  schedule();
}

/* Used to test */
void foo(){
    for(int i = 0; i < 10; ++i) {
        uart_sends("Thread id: ");
        uart_sendi(get_current()->tid);
        uart_sends(", i = ");
        uart_sendi(i);
        uart_sends("\n");
        for(int j = 0; j < 1000000; j++);
        schedule();
    }
}

void thread_test(){
  for(int i = 0; i < 3; ++i){
    thread_create(foo);
  }
  idle();
}
