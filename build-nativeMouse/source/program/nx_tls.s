// Empty TLS template for the libnx code linked into this module.
//
// libnx's thread.o computes the new thread's TLS area from __tls_start /
// __tls_end, the init-image copy from __tdata_lma / __tdata_lma_end, and the
// alignment through __tls_align (which holds the ADDRESS of a word holding
// the alignment, cf. libnx switch.ld). This module's link has no .tdata /
// .tbss content, so start == end everywhere: zero-length template, and the
// alignment word holding 16. All five names point at one retained rodata
// word so --gc-sections cannot drop the backing storage.

    .global __tls_align
    .global __tdata_lma
    .global __tdata_lma_end
    .global __tls_start
    .global __tls_end
    .section .rodata.nx_tls_empty,"a",%progbits
    .align 3
__tls_align:
    .quad 16
    .set __tdata_lma, __tls_align
    .set __tdata_lma_end, __tls_align
    .set __tls_start, __tls_align
    .set __tls_end, __tls_align
