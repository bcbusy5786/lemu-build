/*
 * QEMU VMMouse MMIO transport - absolute mouse backdoor for machines
 * without an x86 vmport (MIPS, Alpha, PowerPC).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HW_INPUT_VMMOUSE_MMIO_H
#define HW_INPUT_VMMOUSE_MMIO_H

#define TYPE_VMMOUSE_MMIO "vmmouse-mmio"

/*
 * Register window (one page). A guest driver maps this at the machine's
 * chosen physical base, confirms the device by reading MAGIC/VERSION, then
 * loads EAX..EDI and strobes COMMAND to run one protocol command; results
 * land back in EAX..EDI. Same wire protocol as the x86 hw/i386/vmmouse.c.
 */
#define VMMOUSE_MMIO_SIZE       0x1000
#define VMMOUSE_MMIO_MAGIC_VAL  0x564D5868   /* 'VMXh' */
#define VMMOUSE_MMIO_IFACE_VER  1

#define VMMOUSE_MMIO_REG_MAGIC      0x00     /* RO */
#define VMMOUSE_MMIO_REG_VERSION    0x04     /* RO */
#define VMMOUSE_MMIO_REG_EAX        0x10     /* RW */
#define VMMOUSE_MMIO_REG_EBX        0x14     /* RW */
#define VMMOUSE_MMIO_REG_ECX        0x18     /* RW */
#define VMMOUSE_MMIO_REG_EDX        0x1c     /* RW */
#define VMMOUSE_MMIO_REG_ESI        0x20     /* RW */
#define VMMOUSE_MMIO_REG_EDI        0x24     /* RW */
#define VMMOUSE_MMIO_REG_COMMAND    0x28     /* WO: strobe -> dispatch */

#endif /* HW_INPUT_VMMOUSE_MMIO_H */
