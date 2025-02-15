#include "exception.h"
#include "initrd.h"
#include "string.h"
#include "mailbox.h"
#include "mini_uart.h"
#include "interrupt.h"
#include "memory.h"
#include "syscall.h"
#include "thread.h"
#include "vm.h"
#include "signal.h"
#include "vfs.h"
#include "tmpfs.h"
#include "initramfs.h"
#include "dev_framebuffer.h"

extern void restore_context(callee_reg *regs);

int sys_getpid()
{
  thread *t = get_current();
  return t->tid;
}

size_t sys_uartread(char buf[], size_t size)
{
  size_t i = 0;
  while (i < size)
  {
    buf[i] = uart_recv();
    i++;
  }
  return i;
}

size_t sys_uartwrite(char buf[], size_t size)
{
  size_t i;
  for (i = 0; i < size; i++)
  {
    uart_sendc(buf[i]);
  }
  return i;
}

int sys_exec(const char *name, char *const argv[])
{
  struct vnode *node;
  int ret = vfs_lookup(name, &node);
  if (ret)
  {
    uart_sends("exec: No such file or directory\n");
    return -1;
  }
  struct initramfs_node *target = (struct initramfs_node *)node->internal;
  void *p = kmalloc(target->size);
  memcpy((void *)p, (void *)target->data, target->size);

  thread *t = get_current();
  clear_vma_list(&t->vma_list);
  clear_pagetable((pagetable_t)t->regs.pgd, 0);

  t->stack = kmalloc(THREAD_STACK_SIZE);
  // memset(t->stack, 0, THREAD_STACK_SIZE);
  add_vma(&t->vma_list, 0x0, virt_to_phys(p), target->size, 0b111);
  add_vma(&t->vma_list, 0x80000, virt_to_phys(thread_wrapper), 0x1000, 0b101);
  add_vma(&t->vma_list, 0xffffffffb000, virt_to_phys(t->stack), 0x4000, 0b111);
  add_vma(&t->vma_list, 0x3c000000, 0x3c000000, 0x3000000, 0b111);
  add_vma(&t->vma_list, 0x100000, virt_to_phys(handler_container), 0x1000, 0b101);

  for (int i = 0; i < MAX_SIGNAL + 1; i++)
  {
    t->signal_handler[i] = 0;
  }

  for(int i = 0; i < THREAD_FD_TABLE_SIZE; i++){
    t->fd_table[i] = 0;
  }

  vfs_open("/dev/uart", 0, &t->fd_table[0]);
  vfs_open("/dev/uart", 0, &t->fd_table[1]);
  vfs_open("/dev/uart", 0, &t->fd_table[2]);

  t->regs.lr = 0x80000;
  t->regs.sp = 0xfffffffff000;
  t->regs.fp = t->regs.sp;

  asm volatile(
      "msr tpidr_el1, %0;"
      "msr spsr_el1, xzr;" // spsr_el1 bit[9:6] are D, A, I, and F; bit[3:0]: el0t
      "msr elr_el1, %1;"
      "msr sp_el0, %2;"
      "mov sp, %3;"
      "dsb ish;"
      "msr ttbr0_el1, %4;"
      "tlbi vmalle1is;"
      "dsb ish;"
      "isb;"
      "eret;" ::"r"(t),
      "r"(t->regs.lr + ((unsigned long)thread_wrapper % 0x1000)), "r"(t->regs.sp), "r"(t->kernel_stack + THREAD_STACK_SIZE), "r"(t->regs.pgd));
  return 0;
}

void set_child_lr(thread *t)
{
  unsigned long ret;
  asm volatile("mov %0, lr" : "=r"(ret));
  t->regs.lr = ret;
  return;
}

// when do system call, the thread->regs should store kernel state.
int sys_fork(trapframe *tf)
{
  thread *cur = get_current();
  thread *child = thread_create(0);
  int child_tid = child->tid;
  unsigned long tmp_pgd = child->regs.pgd;

  memcpy((void *)&child->regs, (void *)&cur->regs, sizeof(callee_reg));
  child->regs.pgd = tmp_pgd;

  // user stack copy
  // memcpy((void*)child->stack, (void*)cur->stack, THREAD_STACK_SIZE);

  // kernel stack copy
  memcpy((void *)child->kernel_stack, (void *)cur->kernel_stack, THREAD_STACK_SIZE);
  // vma_list copy
  copy_vma_list(&child->vma_list, cur->vma_list);

  // page table copy
  copy_pagetable((pagetable_t)child->regs.pgd, (pagetable_t)cur->regs.pgd, 0);
  
  // signal handler copy
  for (int i = 0; i < MAX_SIGNAL + 1; i++)
  {
    child->signal_handler[i] = cur->signal_handler[i];
  }

  // fd_table copy
  for (int i = 0; i < THREAD_FD_TABLE_SIZE; i++)
  {
    child->fd_table[i] = cur->fd_table[i];
  }

  child->cwd = cur->cwd;

  unsigned long cur_sp;
  asm volatile("mov %0, sp;" : "=r"(cur_sp));
  child->regs.sp = (unsigned long)(cur_sp + ((void *)child->kernel_stack - cur->kernel_stack));
  child->regs.fp = child->regs.sp;
  // child->regs.sp = cur_sp;

  trapframe *child_tf = (trapframe *)(child->kernel_stack + ((char *)tf - (char *)cur->kernel_stack));

  child_tf->x[0] = 0;
  // child_tf->sp_el0 = (unsigned long)(child->stack+((void*)tf->sp_el0 - cur->stack));
  child_tf->sp_el0 = tf->sp_el0;
  set_child_lr(child);

  if (get_current()->tid == child_tid)
  {
    return 0;
  }

  tf->x[0] = child_tid;
  return child->tid;
}

int sys_mbox_call(unsigned char ch, unsigned int *mbox)
{

  if (((unsigned long)mbox & VIRT_OFFSET) == 0)
  {
    pte_t *pte = walk((pagetable_t)phys_to_virt((void *)get_current()->regs.pgd), (unsigned long)mbox, 0);
    if (!pte || !(*pte & 0x1))
    {
      uart_sends("mbox: ");
      uart_sendl((unsigned long)mbox);
      uart_sends("\nmbox_call: walk failed\n");
    }

    unsigned long phys = (*pte & 0xfffffffff000) | ((unsigned long)mbox & 0xfff);
    return mailbox_call(ch, (unsigned int *)phys_to_virt((void *)phys));
  }

  return mailbox_call(ch, mbox);
}

void sys_exit(int status)
{
  thread_exit();
}

void sys_kill(int pid)
{
  thread_kill(pid);
}

void sys_signal(int signum, void (*handler)())
{
  thread *t = get_current();
  t->signal_handler[signum] = handler;
}

void posix_kill(int pid, int signum)
{
  thread *t = get_thread(pid);
  if (t)
  {
    t->signal_pending |= 1 << signum;
  }
}

void sys_sigreturn(unsigned long sp_el0)
{
  thread *t = get_current();
  t->signal_processing = 0;
  remove_vma(&t->vma_list, 0x120000);
  // kfree((void*)((sp_el0 % THREAD_STACK_SIZE)?(sp_el0 & ~(THREAD_STACK_SIZE-1)):(sp_el0 - THREAD_STACK_SIZE)));
  restore_context(&t->signal_regs);
}

void *sys_mmap(void *addr, unsigned long len, int prot, int flags, int fd, int file_offset)
{
  thread *t = get_current();
  unsigned long start = (unsigned long)addr & ~(PAGE_SIZE - 1);
  unsigned long size = len % PAGE_SIZE ? ((len / PAGE_SIZE) + 1) * PAGE_SIZE : len;

  vm_area_t *prev = 0, *cur = t->vma_list;
  do
  {
    prev = cur;
    cur = cur->next;
  } while (cur != t->vma_list && cur->va <= (start + size));

  if (!addr || (prev && (prev->va + prev->sz) > start) || (cur && cur->va < (start + size)))
  {
    if (prev)
      start = ((prev->va + prev->sz) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
    else
      start = 0;
  }

  void *p = kmalloc(size);
  memset(p, 0, size);
  add_vma(&t->vma_list, start, virt_to_phys(p), size, prot);
  return (void *)start;
}

int sys_open(const char *pathname, int flags)
{
  thread *t = get_current();
  // uart_sends("[open]: ");
  // uart_sends(pathname);
  // uart_sends("\n");

  for (int i = 0; i < THREAD_FD_TABLE_SIZE; i++)
  {
    if (t->fd_table[i] == 0)
    {
      vfs_open(pathname, flags, &t->fd_table[i]);
      return i;
    }
  }

  return -1;
}

int sys_close(int fd)
{
  // uart_sends("[close]: fd: ");
  // uart_sendi(fd);
  // uart_sends("\n");
  thread *t = get_current();
  vfs_close(t->fd_table[fd]);
  t->fd_table[fd] = 0;
  return 0;
}

long sys_write(int fd, const void *buf, size_t count)
{
  // uart_sends("[write]: fd: ");
  // uart_sendi(fd);
  // uart_sends("\n");
  thread *t = get_current();
  return vfs_write(t->fd_table[fd], buf, count);;
}

long sys_read(int fd, void *buf, size_t count)
{
  // uart_sends("[read]: fd: ");
  // uart_sendi(fd);
  // uart_sends("\n");
  thread *t = get_current();
  return vfs_read(t->fd_table[fd], buf, count);
}

int sys_mkdir(const char *pathname, unsigned mode)
{
  // uart_sends("[mkdir]: ");
  // uart_sends(pathname);
  // uart_sends("\n");
  return vfs_mkdir(pathname);
}

int sys_mount(const char *src, const char *target, const char *filesystem, unsigned long flags, const void *data)
{
  // uart_sends("[mount]: ");
  // uart_sends(target);
  // uart_sends(" with ");
  // uart_sends(filesystem);
  // uart_sends("\n");
  return vfs_mount(target, filesystem);
}

int sys_chdir(const char *path)
{
  // uart_sends("[chdir]: ");
  // uart_sends(path);
  // uart_sends("\n");
  thread *t = get_current();
  vfs_lookup(path, &t->cwd);
  return 0;
}

int sys_lseek64(int fd, long offset, int whence)
{
  thread *t = get_current();
  return vfs_lseek64(t->fd_table[fd], offset, whence);
}

int sys_ioctl(int fd, unsigned long request, void *arg)
{
  if(request == 0)
    return dev_framebuffer_ioctl((struct framebuffer_info*)arg);

  return -1;
}

int getpid()
{
  int ret;
  asm volatile("mov x8, 0;"
               "svc	0;"
               "mov	%0 , x0;"
               : "=r"(ret));
  return ret;
}

int fork()
{
  int ret;
  asm volatile("mov x8, 4;"
               "svc	0;"
               "mov	%0 , x0;"
               : "=r"(ret));
  return ret;
}

void exit(int status)
{
  asm volatile("mov x8, 5;"
               "svc	0;");
}

void sigreturn()
{
  asm volatile(
      "mov x8, 20;"
      "svc 0;");
}

/* For test */

void jump_to_el0()
{
  thread *t = get_current();
  uart_sendi(t->tid);
  for (int i = 0; i < 1000000; i++)
    ;

  asm volatile("msr spsr_el1, %0" ::"r"(0x3c0));    // disable E A I F
  asm volatile("msr elr_el1, %0" ::"r"(fork_test)); // get back to caller function
  asm volatile("msr sp_el0, %0" ::"r"(t->regs.sp));
  asm volatile("mov sp, %0" ::"r"(t->kernel_stack + THREAD_STACK_SIZE));
  asm volatile("eret");
}

void fork_test()
{
  uart_sends("\nFork Test, pid ");

  uart_sendi(getpid());
  uart_sends("\n");
  for (int i = 0; i < 1000000; i++)
    ; // delay
  int cnt = 1;
  int ret = 0;
  if ((ret = fork()) == 0)
  { // child
    long long cur_sp;
    asm volatile("mov %0, sp" : "=r"(cur_sp));
    // printf("first child pid: %d, cnt: %d, ptr: %x, sp : %x\n", get_pid(), cnt, &cnt, cur_sp);
    uart_sends("first child pid: ");
    uart_sendi(getpid());
    uart_sends(", cnt: ");
    uart_sendi(cnt);
    uart_sends(", ptr: ");
    uart_sendh((unsigned long)&cnt);
    uart_sends(", sp: ");
    uart_sendh(cur_sp);
    uart_sends("\n");
    ++cnt;

    if ((ret = fork()) != 0)
    {
      asm volatile("mov %0, sp" : "=r"(cur_sp));
      // printf("first child pid: %d, cnt: %d, ptr: %x, sp : %x\n", get_pid(), cnt, &cnt, cur_sp);
      uart_sends("first child pid: ");
      uart_sendi(getpid());
      uart_sends(", cnt: ");
      uart_sendi(cnt);
      uart_sends(", ptr: ");
      uart_sendh((unsigned long)&cnt);
      uart_sends(", sp: ");
      uart_sendh(cur_sp);
      uart_sends("\n");
    }
    else
    {
      while (cnt < 5)
      {
        asm volatile("mov %0, sp" : "=r"(cur_sp));
        // printf("second child pid: %d, cnt: %d, ptr: %x, sp : %x\n", get_pid(), cnt, &cnt, cur_sp);
        uart_sends("second child pid: ");
        uart_sendi(getpid());
        uart_sends(", cnt: ");
        uart_sendi(cnt);
        uart_sends(", ptr: ");
        uart_sendh((unsigned long)&cnt);
        uart_sends(", sp: ");
        uart_sendh(cur_sp);
        uart_sends("\n");
        ++cnt;
      }
    }
    exit(0);
  }
  else
  {
    // printf("parent here, pid %d, child %d\n", get_pid(), ret);
    uart_sends("parent here, pid ");
    uart_sendi(getpid());
    uart_sends(", child ");
    uart_sendi(ret);
    uart_sends("\n");
    exit(0);
  }
}

void sys_fork_test()
{
  thread_create(jump_to_el0);
  idle();
}
