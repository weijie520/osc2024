.align 11 // vector table should be aligned to 0x800
.global exception_vector_table

exception_vector_table:
  b exception_handler // branch to a handler function.
.align 7 // entry size is 0x80, .align will pad 0
  b exception_handler
.align 7
  b exception_handler // reserved for FIQ(Fast Interrupt)
.align 7
  b exception_handler // reserved for SError
.align 7

  b exception_handler // synchronous: caused by current excution instr.
.align 7
  b irq_handler
.align 7
  b exception_handler // reserved for FIQ; for prioritizing exception
.align 7
  b exception_handler // reserved for SError
.align 7

  b lower_exception_handler
.align 7
  b lower_irq_hadler
.align 7
  b exception_handler // reserved for FIQ
.align 7
  b exception_handler // reserved for SError
.align 7

  b exception_handler
.align 7
  b exception_handler
.align 7
  b exception_handler // reserved for FIQ
.align 7
  b exception_handler // reserved for SError
.align 7

.global set_exception_vector_table
set_exception_vector_table:
  adr x4, exception_vector_table
  msr vbar_el1, x4
  ret

// save general registers to stack
.macro save_all
  sub sp, sp, 32*9
  stp x0, x1, [sp, 16*0]
  stp x2, x3, [sp, 16*1]
  stp x4, x5, [sp, 16*2]
  stp x6, x7, [sp, 16*3]
  stp x8, x9, [sp, 16*4]
  stp x10, x11, [sp, 16*5]
  stp x12, x13, [sp, 16*6]
  stp x14, x15, [sp, 16*7]
  stp x16, x17, [sp, 16*8]
  stp x18, x19, [sp, 16*9]
  stp x20, x21, [sp, 16*10]
  stp x22, x23, [sp, 16*11]
  stp x24, x25, [sp, 16*12]
  stp x26, x27, [sp, 16*13]
  stp x28, x29, [sp, 16*14]
  mrs x0, spsr_el1
  stp x30, x0, [sp, 16*15]
  mrs x0, elr_el1
  mrs x1, sp_el0
  stp x0, x1, [sp, 16*16]
.endm

// load general registers from stack
.macro load_all
  ldp x0, x1, [sp, 16*16]
  msr elr_el1, x0
  msr sp_el0, x1
  ldp x30, x0, [sp, 16*15]
  msr spsr_el1, x0
  ldp x0, x1, [sp, 16*0]
  ldp x2, x3, [sp, 16*1]
  ldp x4, x5, [sp, 16*2]
  ldp x6, x7, [sp, 16*3]
  ldp x8, x9, [sp, 16*4]
  ldp x10, x11, [sp, 16*5]
  ldp x12, x13, [sp, 16*6]
  ldp x14, x15, [sp, 16*7]
  ldp x16, x17, [sp, 16*8]
  ldp x18, x19, [sp, 16*9]
  ldp x20, x21, [sp, 16*10]
  ldp x22, x23, [sp, 16*11]
  ldp x24, x25, [sp, 16*12]
  ldp x26, x27, [sp, 16*13]
  ldp x28, x29, [sp, 16*14]
  add sp, sp, 32*9
.endm

exception_handler:
  save_all
  bl exception_entry
  load_all
  eret

irq_handler:
  save_all
  bl irq_entry
  load_all
  eret

lower_exception_handler:
  save_all
  mov x0, sp
  bl lower_exception_entry
  load_all
  eret

lower_irq_hadler:
  save_all
  bl lower_irq_entry
  load_all
  eret
