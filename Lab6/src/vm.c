#include "vm.h"
#include "mmu.h"
#include "mini_uart.h"
#include "memory.h"
#include "string.h"

unsigned long virt_to_phys(void* vaddr){
  return (unsigned long)vaddr - VIRT_OFFSET;
}

unsigned long phys_to_virt(void* paddr){
  return (unsigned long)paddr + VIRT_OFFSET;
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
  uart_sends("va: ");
  uart_sendl(vaddr);
  uart_sends("\n");
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
    *pte = (pa+i)|PD_PAGE|PD_ACCESS|(MAIR_IDX_NORMAL_NOCACHE << 2)|USER_ACCESS; // should set user access?
  }
  return 0;
}

// int copy_vm(pagetable_t src, pagetable_t dst){
  
// }


void dump_pagetable(pagetable_t pagetable, int level) {
    uart_sends("Level ");
    uart_sendi(level++);
    uart_sends("\n");
    for (int i = 0; i < 512; i++) { // Assuming 512 entries in the page table
        pte_t *pte = &pagetable[i];

        if (*pte & 0x1) {
            uart_sends("Entry ");
            uart_sendi(i);
            uart_sends(": ");
            uart_sendl((unsigned long)*pte);
            uart_sendc('\n');
            if(level <=3 )
              dump_pagetable((pagetable_t)phys_to_virt((void *)(*pte & 0x0000fffffffff000)), level);
        }
    }
    uart_sends("-------------------------\n");
}
