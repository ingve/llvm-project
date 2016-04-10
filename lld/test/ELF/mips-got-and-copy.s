# REQUIRES: mips

# If there are two relocations such that the first one requires
# dynamic COPY relocation, the second one requires GOT entry
# creation, linker should create both - dynamic relocation
# and GOT entry.

# RUN: llvm-mc -filetype=obj -triple=mips-unknown-linux \
# RUN:         %S/Inputs/mips-dynamic.s -o %t.so.o
# RUN: llvm-mc -filetype=obj -triple=mips-unknown-linux %s -o %t.o
# RUN: ld.lld %t.so.o -shared -o %t.so
# RUN: ld.lld %t.o %t.so -o %t.exe
# RUN: llvm-readobj -r -mips-plt-got %t.exe | FileCheck %s

# CHECK:      Relocations [
# CHECK-NEXT:   Section (7) .rel.dyn {
# CHECK-NEXT:     0x{{[0-9A-F]+}} R_MIPS_COPY data0
# CHECK-NEXT:     0x{{[0-9A-F]+}} R_MIPS_COPY data1
# CHECK-NEXT:   }
# CHECK-NEXT: ]
# CHECK-NEXT: Primary GOT {
# CHECK-NEXT:   Canonical gp value: 0x37FF0
# CHECK-NEXT:   Reserved entries [
# CHECK:        ]
# CHECK-NEXT:   Local entries [
# CHECK-NEXT:   ]
# CHECK-NEXT:   Global entries [
# CHECK-NEXT:     Entry {
# CHECK:            Section: .bss
# CHECK-NEXT:       Name: data0
# CHECK-NEXT:     }
# CHECK-NEXT:     Entry {
# CHECK:            Section: .bss
# CHECK-NEXT:       Name: data1
# CHECK-NEXT:     }
# CHECK-NEXT:   ]
# CHECK-NEXT:   Number of TLS and multi-GOT entries: 0
# CHECK-NEXT: }

  .text
  .global __start
__start:
  # Case A: 'got' relocation goes before 'copy' relocation
  lui    $t0,%hi(data0)         # R_MIPS_HI16 - requires R_MISP_COPY relocation
  addi   $t0,$t0,%lo(data0)
  lw     $t0,%got(data0)($gp)   # R_MIPS_GOT16 - requires GOT entry

  # Case B: 'copy' relocation goes before 'got' relocation
  lw     $t0,%got(data1)($gp)   # R_MIPS_GOT16 - requires GOT entry
  lui    $t0,%hi(data1)         # R_MIPS_HI16 - requires R_MISP_COPY relocation
  addi   $t0,$t0,%lo(data1)
