/*
 * ARC FPGA Platform IRQ hookups
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <asm/irq.h>
#include <plat/memmap.h>

static void arc_mask_irq(struct irq_data *data)
{
	arch_mask_irq(data->irq);
}

static void arc_unmask_irq(struct irq_data *data)
{
	arch_unmask_irq(data->irq);
}

/*
 * There's no off-chip Interrupt Controller in the FPGA builds
 * Below sufficies a simple model for the on-chip controller, with
 * all interrupts being level triggered.
 */
static struct irq_chip fpga_chip = {
	.irq_mask	= arc_mask_irq,
	.irq_unmask	= arc_unmask_irq,
};

void __init plat_init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++)
		irq_set_chip_and_handler(i, &fpga_chip, handle_level_irq);

	/*
	 * SMP Hack because UART IRQ hardwired to cpu0 (boot-cpu) but if the
	 * request_irq() comes from any other CPU, the low level IRQ unamsking
	 * essential for getting Interrupts won't be enabled on cpu0, locking
	 * up the UART state machine.
	 */
#ifdef CONFIG_SMP
	arch_unmask_irq(UART0_IRQ);
#endif
}
