#include "vm.h"
#include "mmu.h"
#include "mini_uart.h"
#include "memory.h"
#include "string.h"
#include "thread.h"

unsigned long virt_to_phys(void* vaddr){
  return (unsigned long)vaddr - VIRT_OFFSET;
}

unsigned long phys_to_virt(void* paddr){
  return (unsigned long)paddr + VIRT_OFFSET;
}

void add_vma(vm_area_t **head, unsigned long va, unsigned long pa, unsigned long sz, unsigned long flags){
  vm_area_t *new_area = (vm_area_t*)kmalloc(sizeof(vm_area_t));
  new_area->va = va;
  new_area->pa = pa;
  new_area->sz = sz;
  new_area->flags = flags;

  if(!(*head)){
    new_area->prev = new_area;
    new_area->next = new_area;
    *head = new_area;
  }else{
    vm_area_t *cur = *head;
    do{
      if(cur->va > va){
        break;
      }
      cur = cur->next;
    }while(cur != *head);
    new_area->prev = cur->prev;
    new_area->next = cur;
    cur->prev->next = new_area;
    cur->prev = new_area;
  }
  uart_sends("add_vma: 0x");
  uart_sendl(va);
  uart_sends(" -> 0x");
  uart_sendl(pa);
  uart_sends("\n");
}

void remove_vma(vm_area_t **head, unsigned long va){
  vm_area_t *tmp = *head;
  do{
    if(tmp->va == va){
      if(tmp->next == tmp){
        *head = 0;
      }else{
        tmp->prev->next = tmp->next;
        tmp->next->prev = tmp->prev;
        *head = tmp->next;
      }
      kfree((void*)phys_to_virt((void*)tmp->pa));
      kfree(tmp);
      return;
    }
  }while(tmp != *head);
}

void clear_vma_list(vm_area_t **head){
  do{
    vm_area_t *tmp = *head;
    if(tmp->next == tmp){
      *head = 0;
    }else{
      tmp->prev->next = tmp->next;
      tmp->next->prev = tmp->prev;
      *head = tmp->next;
    }
    kfree((void*)phys_to_virt((void*)tmp->pa));
    kfree(tmp);
  }while(*head != 0);
}

void copy_vma_list(vm_area_t **dst, vm_area_t *src){
  vm_area_t *cur = src;
  do{
    // if(cur->va == 0xffffffffb000) continue; // skip stack
    // skip peripheral
    if(cur->va == 0x3c000000) {
      cur = cur->next;
      continue;
    }
    void *pm = kmalloc(cur->sz);
    memcpy(pm, (void*)phys_to_virt((void*)cur->pa), cur->sz);
    add_vma(dst, cur->va, virt_to_phys(pm), cur->sz, cur->flags);
    cur = cur->next;
  }while(cur != src);
}

/*******************************************************
* Entry of PGD, PUD, PMD which point to a page table
*
* +-----+------------------------------+---------+--+
* |     | next level table's phys addr | ignored |11|
* +-----+------------------------------+---------+--+
*      47                             12         2  0
*
* Entry of PUD, PMD which point to a block
*
* +-----+------------------------------+---------+--+
* |     |  block's physical address    |attribute|01|
* +-----+------------------------------+---------+--+
*      47                              n         2  0
*
* Entry of PTE which points to a page
*
* +-----+------------------------------+---------+--+
* |     |  page's physical address     |attribute|11|
* +-----+------------------------------+---------+--+
*      47                             12         2  0
*
* Invalid entry
*
* +-----+------------------------------+---------+--+
* |     |  page's physical address     |attribute|*0|
* +-----+------------------------------+---------+--+
*      47                             12         2  0
*******************************************************/

pte_t* walk(pagetable_t pgt, unsigned long vaddr, int alloc){
  for(int level = 3; level > 0; level--){
    pte_t* pte = &pgt[((vaddr >> (12 + 9*level)) & 0x1ff)];
    if(((*pte) & 0x1) == 0){
      if(!alloc)
        return 0;
      void* new_pgt = alloc_pages(0);
      memset((void*)new_pgt, 0, PAGE_SIZE);
      *pte = virt_to_phys(new_pgt)|PD_TABLE|PD_ACCESS|(MAIR_IDX_NORMAL_NOCACHE << 2);
    }
    pgt = (pagetable_t)phys_to_virt((void*)(*pte & 0xfffffffff000));
  }
  return &pgt[(vaddr >> 12) & 0x1ff];
}


int map_pages(pagetable_t pgt, unsigned long va, unsigned long pa, unsigned long sz, unsigned long attr){
  if(sz == 0)
    return -1;

  pte_t* pte;
  pagetable_t pgd = (pagetable_t)phys_to_virt((void*)pgt);
  va = va & ~(PAGE_SIZE - 1);
  pa = pa & ~(PAGE_SIZE - 1);

  for(unsigned int i = 0; i < sz; i += PAGE_SIZE){
    pte = walk(pgd, va+i, 1);
    if(pte == 0){
      uart_sends("walk failed\n");
      return -1;
    }
    *pte = (pa+i)|PD_PAGE|PD_ACCESS|PD_KNX|(MAIR_IDX_NORMAL_NOCACHE << 2)|attr; // should set user access?
  }
  return 0;
}

int copy_vm(pagetable_t src, pagetable_t dst, int level){
  pagetable_t src_pgd = (pagetable_t)phys_to_virt((void*)src);
  pagetable_t dst_pgd = (pagetable_t)phys_to_virt((void*)dst);
  for(int i = 0; i < 512; i++){
    pte_t* src_pte = &src_pgd[i];
    if(*src_pte & 0x1){
      void* new_pgt = alloc_pages(0);
      memset((void*)new_pgt, 0, PAGE_SIZE);
      if(level != 3)
        *src_pte = virt_to_phys(new_pgt)|PD_TABLE|PD_ACCESS|(MAIR_IDX_NORMAL_NOCACHE << 2);
      else
        *src_pte = virt_to_phys(new_pgt)|PD_TABLE|PD_ACCESS|(MAIR_IDX_NORMAL_NOCACHE << 2)|USER_ACCESS;
      *dst_pgd = *src_pte;
      copy_vm((pagetable_t)phys_to_virt((void*)(*src_pte & 0xfffffffff000)), (pagetable_t)phys_to_virt((void*)(*dst_pgd & 0xfffffffff000)), ++level);
    }
  }
}

void clear_pagetable(pagetable_t pgt, int level){
  uart_sends("clear_pgt\n");
  pagetable_t pagetable = (pagetable_t)phys_to_virt((void*)pgt);
  for(int i = 0; i < 512; i++){
    pte_t* pte = &pagetable[i];
    if(*pte & 0x1){
      if(level < 2)
        clear_pagetable((pagetable_t)((void*)(*pte & 0xfffffffff000)), level + 1);
      kfree((void*)phys_to_virt((void*)(*pte & 0xfffffffff000)));
      *pte = 0;
    }
  }
}

void dump_pagetable(pagetable_t pgt, int level) {
    uart_sends("Level ");
    uart_sendi(level++);
    uart_sends("\n");
    for (int i = 0; i < 512; i++) { // Assuming 512 entries in the page table
        pte_t *pte = &pgt[i];
        if (*pte & 0x1) {
            uart_sends("Entry ");
            uart_sendi(i);
            uart_sends(": ");
            uart_sendl((unsigned long)*pte);
            uart_sendc('\n');
            if(level <= 3)
              dump_pagetable((pagetable_t)phys_to_virt((void *)(*pte & 0x0000fffffffff000)), level);
        }
    }
    uart_sends("-------------------------\n");
}

void page_fault_handler(){
  thread *t = get_current();

  unsigned long addr;
  asm volatile("mrs %0, FAR_EL1" : "=r"(addr));

  vm_area_t *vma = t->vma_list;
  do{
    if(!vma){
      uart_sends("vma_list is null\n");
      break;
    }
    if(addr >= vma->va && addr < vma->va + vma->sz){
      unsigned long va = addr & ~(PAGE_SIZE - 1);
      unsigned long pa = vma->pa + (va - vma->va);
      unsigned long attr = 0;
      // xwr
      if(!(vma->flags & 4)) attr |= PD_UNX;
      if(!(vma->flags & 2)) attr |= PD_RDONLY;
      if((vma->flags & 1)) attr |= USER_ACCESS;
      if(map_pages((pagetable_t)t->regs.pgd, va, pa, PAGE_SIZE, attr) == 0){
        uart_sends("[Translation fault]: 0x");
        uart_sendl(addr);
        uart_sends(".\n");
      }
      return;
    }
    vma = vma->next;
  }while(vma != t->vma_list);
  uart_sends("error: 0x");
  uart_sendl(addr);
  uart_sends(".\n");
  uart_sends("[Segmentation fault]: Kill Process.\n");
  thread_kill(t->tid);
}
