/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/console.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/mmu_context.h>
#include <linux/kdebug.h>
#include <asm/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/event-log.h>

static inline int notify_page_fault(struct pt_regs *regs, unsigned long cause)
{
	int ret = 0;

#ifdef CONFIG_KPROBES
	if (!user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, cause))
			ret = 1;
		preempt_enable();
	}
#endif

	return ret;
}

static int handle_vmalloc_fault(struct mm_struct *mm, unsigned long address)
{
	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 */
	pgd_t *pgd, *pgd_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd = pgd_offset_fast(mm, address);
	pgd_k = pgd_offset_k(address);

	if (!pgd_present(*pgd_k))
		goto bad_area;

	pud = pud_offset(pgd, address);
	pud_k = pud_offset(pgd_k, address);
	if (!pud_present(*pud_k))
		goto bad_area;

	pmd = pmd_offset(pud, address);
	pmd_k = pmd_offset(pud_k, address);
	if (!pmd_present(*pmd_k))
		goto bad_area;

	set_pmd(pmd, *pmd_k);

	/* XXX: create the TLB entry here */
	return 0;

bad_area:
	return 1;

}
asmlinkage int do_page_fault(struct pt_regs *regs, int write,
			     unsigned long address, unsigned long cause)
{
	struct vm_area_struct *vma = NULL;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	siginfo_t info;
	int fault, ret;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (address >= VMALLOC_START && address <= VMALLOC_END) {
		ret = handle_vmalloc_fault(mm, address);
		if (unlikely(ret))
			goto bad_area_nosemaphore;
		else
			return 0;
	}

	info.si_code = SEGV_MAPERR;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	info.si_code = SEGV_ACCERR;

	/* Handle protection violation, execute on heap or stack */

	if (cause == ((ECR_V_PROTV << 16) | ECR_C_PROTV_INST_FETCH))
		goto bad_area;

	if (write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

survive:
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, write);

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}

	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;

	/* Fault Handled Gracefully, back to what user was trying to do */
	up_read(&mm->mmap_sem);
	return 0;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		tsk->thread.fault_address = address;
		tsk->thread.cause_code = cause;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *)address;
		force_sig_info(SIGSEGV, &info, tsk);
		return 1;
	}

no_context:
	/* Are we prepared to handle this kernel fault?
	 *
	 * (The kernel has valid exception-points in the source
	 *  when it acesses user-memory. When it fails in one
	 *  of those points, we find it in a table and do a jump
	 *  to some fixup code that loads an appropriate error
	 *  code)
	 */
	if (fixup_exception(regs))
		return 0;

	die("Oops", regs, address, cause);

out_of_memory:
	if (is_global_init(tsk)) {
		yield();
		goto survive;
	}
	up_read(&mm->mmap_sem);

	if (user_mode(regs))
		do_group_exit(SIGKILL);	/* This will never return */

	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	if (!user_mode(regs))
		goto no_context;

	tsk->thread.fault_address = address;
	tsk->thread.cause_code = cause;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info(SIGBUS, &info, tsk);

	return 1;
}
