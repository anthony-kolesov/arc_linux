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
#include <asm/sections.h>
#include <asm/irq.h>

/*
 * Early Interrupt sub-system setup
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 * -Platform Independent (must for any ARC700)
 * -Needed for each CPU (hence not foldable into init_IRQ)
 *
 * what it does ?
 * -setup Vector Table Base Reg - in case Linux not linked at 0x8000_0000
 * -Disable all IRQs (on CPU side)
 */
void __init arc_init_IRQ(void)
{
	int level_mask = level_mask;

	write_aux_reg(AUX_INTR_VEC_BASE, _int_vec_base_lds);

	/* Disable all IRQs: enable them as devices request */
	write_aux_reg(AUX_IENABLE, 0);
}

/*
 * Late Interrupt system init called from start_kernel for Boot CPU only
 *
 * Since slab must already be initialized, platforms can start doing any
 * needed request_irq( )s
 */
void __init init_IRQ(void)
{
	const int irq = TIMER0_IRQ;

	/*
	 * Each CPU needs to register irq of it's private TIMER0.
	 * The APIs request_percpu_irq()/enable_percpu_irq() will not be
	 * functional, if we don't "prep" the generic IRQ sub-system with
	 * the following:
	 * -Ensure that devid passed to request_percpu_irq() is indeed per cpu
	 * -disable NOAUTOEN, w/o which the device handler never gets called
	 */
	irq_set_percpu_devid(irq);
	irq_modify_status(irq, IRQ_NOAUTOEN, 0);

	plat_init_IRQ();
}

/*
 * "C" Entry point for any ARC ISR, called from low level vector handler
 */
void arch_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}

int __init get_hw_config_num_irq(void)
{
	uint32_t val = read_aux_reg(ARC_REG_VECBASE_BCR);

	switch (val & 0x03) {
	case 0:
		return 16;
	case 1:
		return 32;
	case 2:
		return 8;
	default:
		return 0;
	}

	return 0;
}

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
