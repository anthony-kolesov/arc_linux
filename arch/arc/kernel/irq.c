/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/irqflags.h>
#include <asm/arcregs.h>

void arch_local_irq_enable(void)
{

	unsigned long flags;
	flags = arch_local_save_flags();
	flags |= (STATUS_E1_MASK | STATUS_E2_MASK);

	/*
	 * If called from hard ISR (between irq_enter and irq_exit)
	 * don't allow Level 1. In Soft ISR we allow further Level 1s
	 */

	if (in_irq())
		flags &= ~(STATUS_E1_MASK | STATUS_E2_MASK);

	arch_local_irq_restore(flags);
}
EXPORT_SYMBOL(arch_local_irq_enable);
