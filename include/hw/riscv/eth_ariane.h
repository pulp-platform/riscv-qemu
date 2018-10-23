/*
 * Ariane machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2018 Florian Zaruba, zarubaf@iis.ee.ethz.ch
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

#ifndef HW_ETH_ARIANE_H
#define HW_ETH_ARIANE_H

typedef struct ETHArianeState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    void *fdt;
    int fdt_size;
} ETHArianeState;

enum {
    ETH_ARIANE_DEBUG,
    ETH_ARIANE_MROM,
    ETH_ARIANE_CLINT,
    ETH_ARIANE_PLIC,
    ETH_ARIANE_UART0,
    ETH_ARIANE_DRAM
};

enum {
    ETH_ARIANE_UART0_IRQ = 1,
};

enum {
    SIFIVE_U_CLOCK_FREQ = 1000000000
};


#define ETH_ARIANE_PLIC_HART_CONFIG "MS"
#define ETH_ARIANE_PLIC_NUM_SOURCES 2
#define ETH_ARIANE_PLIC_NUM_PRIORITIES 7
#define ETH_ARIANE_PLIC_PRIORITY_BASE 0x0
#define ETH_ARIANE_PLIC_PENDING_BASE 0x1000
#define ETH_ARIANE_PLIC_ENABLE_BASE 0x2000
#define ETH_ARIANE_PLIC_ENABLE_STRIDE 0x80
#define ETH_ARIANE_PLIC_CONTEXT_BASE 0x200000
#define ETH_ARIANE_PLIC_CONTEXT_STRIDE 0x1000

#define ARIANE_CPU TYPE_RISCV_CPU_ARIANE

#endif
