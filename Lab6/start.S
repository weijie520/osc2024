#include "include/mmu.h"
.section ".text.boot"

.global _start

_start:

/* get cpu id */
  mrs x1, mpidr_el1
  and x1, x1, #3
  cbnz x1, halt // halt if cpu id != 0

  bl from_el2_to_el1

/* set stack pointer */
  ldr x1, =_start
  mov sp, x1

/* enable mmu */
  ldr x1, = TCR_CONFIG_DEFAULT
  msr tcr_el1, x1

  ldr x1, =(\
(MAIR_DEVICE_nGnRnE<<(MAIR_IDX_DEVICE_nGnRnE*8))|\
(MAIR_NORMAL_NOCACHE<<(MAIR_IDX_NORMAL_NOCACHE*8))\
)
  msr mair_el1, x1

  mov x1, 0 // PGD's page frame at 0x0
  mov x2, 0x1000 // PUD's page frame at 0x1000

  ldr x3, = BOOT_PGD_ATTR
  orr x3, x2, x3 // combine the physical address of next level page with attribute.
  str x3, [x1]

  ldr x3, = BOOT_PUD_ATTR
  mov x4, 0x00000000
  orr x4, x3, x4
  str x4, [x2]// 1st 1GB mapped by the 1st entry of PUD
  mov x4, 0x40000000
  orr x4, x3, x4
  str x4, [x2, 8]// 2nd 1GB mapped by the 2nd entry of PUD

  msr ttbr0_el1, x1 // load PGD to the bottom translation-based register.

  mrs x3, sctlr_el1
  orr x3, x3, 1
  msr sctlr_el1, x3 // enable MMU, cache remains disabled

  msr ttbr0_el1, x1
  msr ttbr1_el1, x1 // also load PGD to the upper translation based register.
  mrs x3, sctlr_el1
  orr x3, x3, 1
  msr sctlr_el1, x3

  ldr x2, = boot_rest // indirect branch to the virtual address
  br x2

boot_rest:
/* clear bss section */
  ldr x1, =bss_begin
  ldr w2, =bss_size

bss_init:
  cbz w2, run_main
  str xzr, [x1], #8
  sub w2, w2, #1
  cbnz w2, bss_init

run_main:
/* branch to main function */
  bl main

halt:
  wfe
  b halt

from_el2_to_el1:
  mov x4, (1<<31) // EL1 uses aarch64
  msr hcr_el2, x4 // hcr_el2's bit31 is RW: 0->lower levels are all aarch32; 1->EL1: aarch64,
// EL0 excution state determined by PSTATE.nRW value
  mov x4, 0x3c5 // EL1h (SPSel = 1) with interrupt disabled
  msr spsr_el2, x4 // 001111000101, spsr_el2 bit[9:6] are D, A, I, and F respectly,
// bit[3:0]: select stack pointer:0101->el1 with sp_el1
  msr elr_el2, lr
  eret // return to EL1
