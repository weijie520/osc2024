#ifndef __VM_H__
#define __VM_H__

#define VIRT_OFFSET 0xFFFF000000000000

typedef unsigned long pte_t;
typedef pte_t* pagetable_t;

unsigned long virt_to_phys(void* vaddr);
unsigned long phys_to_virt(void* paddr);

pte_t* walk(pagetable_t pgt, unsigned long vaddr, int alloc);
int map_pages(pagetable_t pgt, unsigned long va, unsigned long pa, unsigned long sz, unsigned long attr);

void dump_pagetable(pagetable_t pagetable, int level);

#endif