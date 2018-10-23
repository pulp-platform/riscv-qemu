/*
 * QEMU RISC-V Board Compatible with Ariane SoC
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2018 Florian Zaruba, zaruabf@iis.ee.ethz.ch
 *
 * Provides a board compatible with the SiFive Freedom U SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 *
 * This board currently uses a hardcoded devicetree that indicates one hart.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_prci.h"
#include "hw/riscv/eth_ariane.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "exec/address-spaces.h"
#include "elf.h"

#include <libfdt.h>

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sifive_u_memmap[] = {
    [ETH_ARIANE_DEBUG] =    {        0x0,      0x100 },
    [ETH_ARIANE_MROM] =     {     0x1000,    0x11000 },
    [ETH_ARIANE_CLINT] =    {  0x2000000,    0x10000 }, /* sifive_u */
    [ETH_ARIANE_PLIC] =     {  0xc000000,  0x4000000 },
    [ETH_ARIANE_UART0] =    { 0x10000000,     0x1000 },
    [ETH_ARIANE_DRAM] =     { 0x80000000,        0x0 }
};

static uint64_t load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf(kernel_filename, NULL, NULL,
                 &kernel_entry, NULL, &kernel_high,
                 0, EM_RISCV, 1, 0) < 0) {
        error_report("qemu: could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void create_fdt(ETHArianeState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "ucbbar,spike-bare,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "ucbbar,spike-bare-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[ETH_ARIANE_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[ETH_ARIANE_DRAM].base >> 32, memmap[ETH_ARIANE_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->cpus.num_harts - 1; cpu >= 0; cpu--) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->cpus.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                              SIFIVE_U_CLOCK_FREQ);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", 1);
        qemu_fdt_setprop_cell(fdt, intc, "linux,phandle", 1);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells =  g_new0(uint32_t, s->cpus.num_harts * 4);
    for (cpu = 0; cpu < s->cpus.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/clint@%lx",
        (long)memmap[ETH_ARIANE_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ETH_ARIANE_CLINT].base,
        0x0, memmap[ETH_ARIANE_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->cpus.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    cells =  g_new0(uint32_t, s->cpus.num_harts * 4);
    for (cpu = 0; cpu < s->cpus.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
        (long)memmap[ETH_ARIANE_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->cpus.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ETH_ARIANE_PLIC].base,
        0x0, memmap[ETH_ARIANE_PLIC].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,max-priority", 7);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0x35);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 2);
    qemu_fdt_setprop_cells(fdt, nodename, "linux,phandle", 2);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/uart@%lx",
        (long)memmap[ETH_ARIANE_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16750");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ETH_ARIANE_UART0].base,
        0x0, memmap[ETH_ARIANE_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 3686400);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", ETH_ARIANE_UART0_IRQ);
    qemu_fdt_setprop_cells(fdt, nodename, "reg-shift", 2);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
    g_free(nodename);
}

static void riscv_sifive_u_generic_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sifive_u_memmap;
    ETHArianeState *s = g_new0(ETHArianeState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);

    int i;

    /* Initialize harts */
    object_initialize(&s->cpus, sizeof(s->cpus), TYPE_RISCV_HART_ARRAY);
    object_property_add_child(OBJECT(machine), "cpus", OBJECT(&s->cpus),
                              &error_abort);
    object_property_set_str(OBJECT(&s->cpus), ARIANE_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->cpus), true, "realized",
                            &error_abort);

    /* register RAM */
    memory_region_init_ram(main_mem, NULL, "riscv.sifive.u.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ETH_ARIANE_DRAM].base,
                                main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                    /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                    /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                    /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                    /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                    /*     ld     t0, 24(t0) */
#endif
        0x00028067,                    /*     jr     t0 */
        0x00000000,
        memmap[ETH_ARIANE_DRAM].base, /* start: .dword DRAM_BASE */
        0x00000000,
                                       /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[ETH_ARIANE_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[ETH_ARIANE_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[ETH_ARIANE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.sifive.u.mrom",
                           memmap[ETH_ARIANE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ETH_ARIANE_MROM].base,
                                mask_rom);

    /* MMIO */
    s->plic = sifive_plic_create(memmap[ETH_ARIANE_PLIC].base,
        (char *)ETH_ARIANE_PLIC_HART_CONFIG,
        ETH_ARIANE_PLIC_NUM_SOURCES,
        ETH_ARIANE_PLIC_NUM_PRIORITIES,
        ETH_ARIANE_PLIC_PRIORITY_BASE,
        ETH_ARIANE_PLIC_PENDING_BASE,
        ETH_ARIANE_PLIC_ENABLE_BASE,
        ETH_ARIANE_PLIC_ENABLE_STRIDE,
        ETH_ARIANE_PLIC_CONTEXT_BASE,
        ETH_ARIANE_PLIC_CONTEXT_STRIDE,
        memmap[ETH_ARIANE_PLIC].size);

    // UART
    serial_mm_init(system_memory, memmap[ETH_ARIANE_UART0].base,
        2, qdev_get_gpio_in(DEVICE(s->plic), ETH_ARIANE_UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    sifive_clint_create(memmap[ETH_ARIANE_CLINT].base,
        memmap[ETH_ARIANE_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE,
        SIFIVE_TIMECMP_BASE,
        SIFIVE_TIME_BASE);
}

static void riscv_eth_ariane_init(MachineState *machine)
{
    riscv_sifive_u_generic_init(machine);
}


static void riscv_sifive_u_machine_init(MachineClass *mc)
{
    mc->desc = "Ariane emulation platform";
    mc->init = riscv_eth_ariane_init;
    mc->max_cpus = 1;
}

DEFINE_MACHINE("eth_ariane", riscv_sifive_u_machine_init)
