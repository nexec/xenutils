.section .kernel_domd,"a"

/* DomD binary from include/ dir */
.incbin		"domain_bins/domd_linux.bin"

.section .uboot_domu,"a"

/* DomD binary from include/ dir */
.incbin		"domain_bins/u-boot.bin"

.section .dtb_domd,"a"

/* DomD dtb from include/ dir */
.incbin		"domain_bins/domd_kernel.dtb"

.section .dtb_domu,"a"
/* DomU dtb from include/ dir */
.incbin		"domain_bins/uboot_minimal_domu.dtb"
