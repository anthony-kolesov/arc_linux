/*
 * ARC FPGA Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/console.h>
#include <asm/serial.h>
#include <plat/irq.h>
#include <plat/memmap.h>

/*-----------------------BVCI Latency Unit -----------------------------*/

#ifdef CONFIG_ARC_HAS_BVCI_LAT_UNIT

int lat_cycles = CONFIG_BVCI_LAT_CYCLES;

/* BVCI Bus Profiler: Latency Unit */
static void __init setup_bvci_lat_unit(void)
{
#define MAX_BVCI_UNITS 12

	volatile unsigned int *base = (unsigned int *)BVCI_LAT_UNIT_BASE;
	volatile unsigned int *lat_unit = (unsigned int *)base + 21;
	volatile unsigned int *lat_val = (unsigned int *)base + 22;
	unsigned int unit;
	const unsigned long units_req = CONFIG_BVCI_LAT_UNITS;

	/*
	 * There are multiple Latency Units corresponding to the many
	 * interfaces of the system bus arbiter (both CPU side as well as
	 * the peripheral side).
	 *
	 * Unit  0 - System Arb and Mem Controller - adds latency to all
	 * 	    memory trasactions
	 * Unit  1 - I$ and System Bus
	 * Unit  2 - D$ and System Bus
	 * ..
	 * Unit 12 - IDE Disk controller and System Bus
	 *
	 * The programmers model requires writing to lat_unit reg first
	 * and then the latency value (cycles) to lat_value reg
	 */

	if (CONFIG_BVCI_LAT_UNITS == 0) {
		*lat_unit = 0;
		*lat_val = lat_cycles;
		pr_info("BVCI Latency for all Memory Transactions %d cycles\n",
			lat_cycles);
	}
	else {
		for_each_set_bit(unit, &units_req, MAX_BVCI_UNITS) {
			*lat_unit = unit + 1;  /* above returns 0 based */
			*lat_val = lat_cycles;
			pr_info("BVCI Latency for Unit[%d] = %d cycles\n",
				(unit + 1), lat_cycles);
		}
	}
}
#else
static void __init setup_bvci_lat_unit(void)
{
}
#endif

/*----------------------- Platform Devices -----------------------------*/

#ifdef CONFIG_SERIAL_ARC

static unsigned long arc_uart_info[] = {
	CONFIG_ARC_SERIAL_BAUD, CONFIG_ARC_PLAT_CLK, 0
};

#define ARC_UART_DEV(n)					\
							\
static struct resource arc_uart##n##_res[] = {		\
	{						\
		.start = UART##n##_BASE,			\
		.end   = UART##n##_BASE + 0xFF,		\
		.flags = IORESOURCE_MEM,		\
	},						\
	{						\
		.start = UART##n##_IRQ,			\
		.end   = UART##n##_IRQ,			\
		.flags = IORESOURCE_IRQ,		\
	},						\
};							\
							\
static struct platform_device arc_uart##n##_dev = {	\
	.name = "arc-uart",				\
	.id = n,					\
	.num_resources = ARRAY_SIZE(arc_uart##n##_res),	\
	.resource = arc_uart##n##_res,			\
	.dev = {					\
		.platform_data = &arc_uart_info,	\
	},						\
}

ARC_UART_DEV(0);
#if CONFIG_SERIAL_ARC_NR_PORTS > 1
ARC_UART_DEV(1);
#endif

static struct platform_device *fpga_early_devs[] __initdata = {
#if defined(CONFIG_SERIAL_ARC_CONSOLE)
	&arc_uart0_dev,
#endif
};

#endif	/* CONFIG_SERIAL_ARC */

/*
 * Early Platform Initialization called from setup_arch()
 */
void __init arc_platform_early_init(void)
{
	pr_info("[plat-arcfpga]: registering early dev resources\n");

	setup_bvci_lat_unit();

#ifdef CONFIG_SERIAL_ARC
	early_platform_add_devices(fpga_early_devs,
				   ARRAY_SIZE(fpga_early_devs));

	/*
	 * ARC console driver registers itself as an early platform driver
	 * of class "earlyprintk".
	 * Install it here, followed by probe of devices.
	 * The installation here doesn't require earlyprintk in command line
	 * To do so however, replace the lines below with
	 *	parse_early_param();
	 *	early_platform_driver_probe("earlyprintk", 1, 1);
	 *						      ^^
	 */
	early_platform_driver_register_all("earlyprintk");
	early_platform_driver_probe("earlyprintk", 1, 0);

	/*
	 * This is to make sure that arc uart would be preferred console
	 * despite one/more of following:
	 *   -command line lacked "console=ttyS0" or
	 *   -CONFIG_VT_CONSOLE was enabled (for no reason whatsoever)
	 * Note that this needs to be done after above early console is reg,
	 * otherwise the early console never gets a chance to run.
	 */
	add_preferred_console("ttyS", 0, "115200");
#endif
}

#ifdef CONFIG_ARC_PS2
static struct resource arc_ps2_resources[] = {
	[0] = {
		.name  = "arc_ps2_regs",
		.start = PS2_BASE_ADDR,
		.end   = PS2_BASE_ADDR + (5 * 4) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = "arc_ps2_irq",
		.start = PS2_IRQ,
		.end   = PS2_IRQ,
		.flags = IORESOURCE_IRQ,
	 },
};

static struct platform_device arc_ps2_device = {
	.name          = "arc_ps2",
	.resource      = arc_ps2_resources,
	.num_resources = ARRAY_SIZE(arc_ps2_resources),
};
#endif /* CONFIG_ARC_PS2 */

static struct platform_device *fpga_devs[] __initdata = {
#if defined(CONFIG_SERIAL_ARC)
	&arc_uart0_dev,
#if CONFIG_SERIAL_ARC_NR_PORTS > 1
	&arc_uart1_dev,
#endif
#endif

#if defined(CONFIG_ARC_PS2)
	&arc_ps2_device,
#endif
};

int __init fpga_plat_init(void)
{
	pr_info("[plat-arcfpga]: registering device resources\n");

	platform_add_devices(fpga_devs, ARRAY_SIZE(fpga_devs));

	return 0;
}
arch_initcall(fpga_plat_init);
