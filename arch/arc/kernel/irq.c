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
 * -Optionally, setup the High priority Interrupts as Level 2 IRQs
 */
void __init arc_init_IRQ(void)
{
	int level_mask = level_mask;

	write_aux_reg(AUX_INTR_VEC_BASE, _int_vec_base_lds);

	/* Disable all IRQs: enable them as devices request */
	write_aux_reg(AUX_IENABLE, 0);

       /* setup any high priority Interrupts (Level2 in ARCompact jargon) */
#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
#ifdef CONFIG_ARC_IRQ3_LV2
	level_mask |= (1 << 3);
#endif
#ifdef CONFIG_ARC_IRQ5_LV2
	level_mask |= (1 << 5);
#endif
#ifdef CONFIG_ARC_IRQ6_LV2
	level_mask |= (1 << 6);
#endif

	if (level_mask) {
		pr_info("Level-2 interrupts bitset %x\n", level_mask);
		write_aux_reg(AUX_IRQ_LEV, level_mask);
	}
#endif
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

/*
 * arch_local_irq_enable - Enable interrupts.
 *
 * 1. Explicitly called to re-enable interrupts
 * 2. Implicitly called from spin_unlock_irq, write_unlock_irq etc
 *    which maybe in hard ISR itself
 *
 * Semantics of this function change depending on where it is called from:
 *
 * -If called from hard-ISR, it must not invert interrupt priorities
 *  e.g. suppose TIMER is high priority (Level 2) IRQ
 *    Time hard-ISR, timer_interrupt( ) calls spin_unlock_irq several times.
 *    Here local_irq_enable( ) shd not re-enable lower priority interrupts
 * -If called from soft-ISR, it must re-enable all interrupts
 *    soft ISR are low prioity jobs which can be very slow, thus all IRQs
 *    must be enabled while they run.
 *    Now hardware context wise we may still be in L2 ISR (not done rtie)
 *    still we must re-enable both L1 and L2 IRQs
 *  Another twist is prev scenario with flow being
 *     L1 ISR ==> interrupted by L2 ISR  ==> L2 soft ISR
 *     here we must not re-enable Ll as prev Ll Interrupt's h/w context will get
 *     over-written (this is deficiency in ARC700 Interrupt mechanism)
 */

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS	/* Complex version for 2 IRQ levels */

void arch_local_irq_enable(void)
{

	unsigned long flags;
	flags = arch_local_save_flags();

	/* Allow both L1 and L2 at the onset */
	flags |= (STATUS_E1_MASK | STATUS_E2_MASK);

	/* Called from hard ISR (between irq_enter and irq_exit) */
	if (in_irq()) {

		/* If in L2 ISR, don't re-enable any further IRQs as this can
		 * cause IRQ priorities to get upside down. e.g. it could allow
		 * L1 be taken while in L2 hard ISR which is wrong not only in
		 * theory, it can also cause the dreaded L1-L2-L1 scenario
		 */
		if (flags & STATUS_A2_MASK)
			flags &= ~(STATUS_E1_MASK | STATUS_E2_MASK);

		/* Even if in L1 ISR, allowe Higher prio L2 IRQs */
		else if (flags & STATUS_A1_MASK)
			flags &= ~(STATUS_E1_MASK);
	}

	/* called from soft IRQ, ideally we want to re-enable all levels */

	else if (in_softirq()) {

		/* However if this is case of L1 interrupted by L2,
		 * re-enabling both may cause whaco L1-L2-L1 scenario
		 * because ARC700 allows level 1 to interrupt an active L2 ISR
		 * Thus we disable both
		 * However some code, executing in soft ISR wants some IRQs
		 * to be enabled so we re-enable L2 only
		 *
		 * How do we determine L1 intr by L2
		 *  -A2 is set (means in L2 ISR)
		 *  -E1 is set in this ISR's pt_regs->status32 which is
		 *      saved copy of status32_l2 when l2 ISR happened
		 */
		struct pt_regs *pt = get_irq_regs();
		if ((flags & STATUS_A2_MASK) && pt &&
		    (pt->status32 & STATUS_A1_MASK)) {
			/*flags &= ~(STATUS_E1_MASK | STATUS_E2_MASK); */
			flags &= ~(STATUS_E1_MASK);
		}
	}

	arch_local_irq_restore(flags);
}

#else /* ! CONFIG_ARC_COMPACT_IRQ_LEVELS */

/*
 * Simpler version for only 1 level of interrupt
 * Here we only Worry about Level 1 Bits
 */
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
#endif
EXPORT_SYMBOL(arch_local_irq_enable);
