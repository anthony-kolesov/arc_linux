/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: March 2009
 *  -Cleaned up copy_thread( ), made it use task_pt_regs( )
 *  -It used to leave a margin of 8 bytes which was confusing
 *
 * Amit Bhor, Kanika Nema: Codito Technologies 2004
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/a.out.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/elf.h>
#include <linux/tick.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/bug.h>
#include <asm/switch_to.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/pgalloc.h>
#include <asm/arcregs.h>

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
static inline void arch_idle(void)
{
	__asm__("sleep");
}

void cpu_idle(void)
{
        /* Since we SLEEP in idle loop, TIF_POLLING_NRFLAG can't be set */

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_stop_sched_tick(1);

		/* Test the need-resced "flag" of current task "idle"
		   A local ISR or peer CPU may want resched
		   If not set, go to low power friendly "sleep"
		 */

		while (!need_resched())
			arch_idle();

		tick_nohz_restart_sched_tick();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void kernel_thread_helper(void)
{
	__asm__ __volatile__(
		"mov   %r0, %r2	\n\t"
		"mov   %r1, %r3	\n\t"
		"j     [%r1]	\n\t");
}

int kernel_thread(int (*fn) (void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.r2 = (unsigned long)arg;
	regs.r3 = (unsigned long)fn;
	regs.blink = (unsigned long)do_exit;
	regs.ret = (unsigned long)kernel_thread_helper;
	regs.status32 = read_aux_reg(0xa);

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL,
		       NULL);

}
EXPORT_SYMBOL(kernel_thread);

asmlinkage int sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

asmlinkage int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp, regs, 0,
		       NULL, NULL);
}

/* Per man, C-lib clone( ) is as follows
 *
 * int clone(int (*fn)(void *), void *child_stack,
 *           int flags, void *arg, ...
 *           pid_t *ptid, struct user_desc *tls, pid_t *ctid);
 *
 * @fn and @arg are of userland thread-hnalder and thus of no use
 * in sys-call, hence excluded in sys_clone arg list.
 * The only addition is ptregs, needed by fork core, although now-a-days
 * task_pt_regs() can be called anywhere to get that.
 */
asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 int __user *parent_tidptr, void *tls,
			 int __user *child_tidptr, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0, parent_tidptr,
		       child_tidptr);
}

asmlinkage void ret_from_fork(void);

/* Layout of Child kernel mode stack as setup at the end of this function is
 *
 * |     ...        |
 * |     ...        |
 * |    unused      |
 * |                |
 * ------------------  <==== top of Stack (thread.ksp)
 * |   UNUSED 1 word|
 * ------------------
 * |     r25        |
 * ~                ~
 * |    --to--      |   (CALLEE Regs of user mode)
 * |     r13        |
 * ------------------
 * |     fp         |
 * |    blink       |   @ret_from_fork
 * ------------------
 * |                |
 * ~                ~
 * ~                ~
 * |                |
 * ------------------
 * |     r12        |
 * ~                ~
 * |    --to--      |   (scratch Regs of user mode)
 * |     r0         |
 * ------------------
 * |   UNUSED 1 word|
 * ------------------  <===== END of PAGE
 */
int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long topstk,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *child_ptregs;
	struct callee_regs *child_cregs, *parent_cregs;
	unsigned long *childksp;

	/*
	 * Note that parent's pt_regs are passed to this function.
	 * They may not be same as one returned by task_pt_regs(current)
	 * _IF_ we are starting a kernel thread, because it doesn't have a
	 * parent in true sense
	 */
	if (user_mode(regs))
		BUG_ON(regs != task_pt_regs(current));

	/****************************************************************
	 * setup child for its first ever return to user land
	 ****************************************************************/

	/* Copy parents pt regs on child's kernel mode stack */
	child_ptregs = task_pt_regs(p);
	*child_ptregs = *regs;

	/* its kernel mode SP is now @ start of pt_regs */
	childksp = (unsigned long *)child_ptregs;

	/* fork return value 0 for the child */
	child_ptregs->r0 = 0;

	/* I the parent is a kernel thread then we change the stack pointer
	 * of the child its kernel stack
	 */
	if (!user_mode(regs)) {
		/* Arc gcc stack adjustment */
		child_ptregs->sp =
		    (unsigned long)task_thread_info(p) + (THREAD_SIZE - 4);
	} else
		child_ptregs->sp = usp;

	/****************************************************************
	 * setup child for its first kernel mode execution,
	 * to be "switched-in" by schedular and have ability to unwind
	 * out of schedular
	 ****************************************************************/

	/*
	 * push frame pointer and return address (blink) as expected by
	 * __switch_to on top of pt_regs
	 */

	*(--childksp) = (unsigned long)ret_from_fork;	/* push blink */
	*(--childksp) = 0;	/* push fp */

	/* copy CALLEE regs now, on top of above 2 and pt_regs */
	child_cregs = ((struct callee_regs *)childksp) - 1;

	/* For kernel threads, callee regs were not saved to begin with */
	if (user_mode(regs)) {
		parent_cregs = ((struct callee_regs *)regs) - 1;
		*child_cregs = *parent_cregs;
	}

	/*
	 * The kernel SP for child has grown further up, now it is
	 * at the start of where CALLEE Regs were copied.
	 * When child is passed to schedule( ) for the very first time,
	 * it unwinds stack, loading CALLEE Regs from top and goes it's
	 * merry way
	 */
	p->thread.ksp = (unsigned long)child_cregs;	/* THREAD_KSP */

#ifdef CONFIG_ARC_CURR_IN_REG
	/* Replicate parent's user mode r25 for child */
	p->thread.user_r25 = current->thread.user_r25;
#endif

#ifdef CONFIG_ARC_TLS_REG_EMUL
	if (user_mode(regs)) {
		if (unlikely(clone_flags & CLONE_SETTLS)) {
			/*
			 * set task's userland tls data ptr from 4th arg
			 * clone C-lib call is difft from clone sys-call
			 */
			task_thread_info(p)->thr_ptr = regs->r3;
		} else {
			/* Normal fork case: set parent's TLS ptr in child */
			task_thread_info(p)->thr_ptr =
			    task_thread_info(current)->thr_ptr;
		}
	}
#endif

	return 0;
}

int sys_execve(const char __user *filenamei, const char __user *__user *argv,
	       const char __user *__user *envp, struct pt_regs *regs)
{
	int error;
	char __user *filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

/*
 * Some archs flush debug and FPU info here
 */
void flush_thread(void)
{
}

/*
 * Free any architecture-specific thread data structures, etc.
 */
void exit_thread(void)
{
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t * fpu)
{
	return 0;
}

/*
 * API: expected by schedular Code: If thread is sleeping where is that.
 * What is this good for? it will be always the scheduler or ret_from_fork.
 * So we hard code that anyways.
 */
unsigned long thread_saved_pc(struct task_struct *t)
{
	struct pt_regs *regs = task_pt_regs(t);
	unsigned long blink = 0;

	/*
	 * If the thread being queried for in not itself calling this, then it
	 * implies it is not executing, which in turn implies it is sleeping,
	 * which in turn implies it got switched OUT by the schedular.
	 * In that case, it's kernel mode blink can reliably retrieved as per
	 * the picture above (right above pt_regs).
	 */
	if (t != current && t->state != TASK_RUNNING)
		blink = *((unsigned int *)((unsigned long)regs - 4));

	return blink;
}

int sys_arc_settls(void *user_tls_data_ptr)
{
#ifdef CONFIG_ARC_TLS_REG_EMUL
	task_thread_info(current)->thr_ptr = (unsigned int)user_tls_data_ptr;
	return 0;
#else
	return -EFAULT;
#endif
}

/*
 * We return the user space TLS data ptr as sys-call return code
 * Ideally it should be copy to user.
 * However we can cheat by the fact that some sys-calls do return
 * absurdly high values
 * Since the tls dat aptr is not going to be in range of 0xFFFF_xxxx
 * it won't be considered a sys-call error
 * and it will be loads better than copy-to-user, which is a definite
 * D-TLB Miss
 */
int sys_arc_gettls(void)
{
#ifdef CONFIG_ARC_TLS_REG_EMUL
	return task_thread_info(current)->thr_ptr;
#else
	return -EFAULT;
#endif
}

int kernel_execve(const char *filename, const char *const argv[],
		  const char *const envp[])
{
	/*
	 * Although the arguments (order, number) to this function are
	 * same as sys call, we don't need to setup args in regs again.
	 * However in case mainline kernel changes the order of args to
	 * kernel_execve, that assumtion will break.
	 * So to be safe, let gcc know the args for sys call.
	 * If they match no extra code will be generated
	 */
	register int arg2 asm("r1") = (int)argv;
	register int arg3 asm("r2") = (int)envp;

	register int filenm_n_ret asm("r0") = (int)filename;

	__asm__ __volatile__(
		"mov   %r8, %1	\n\t"
		"trap0		\n\t"
		: "+r"(filenm_n_ret)
		: "i"(__NR_execve), "r"(arg2), "r"(arg3)
		: "r8", "memory");

	return filenm_n_ret;
}
EXPORT_SYMBOL(kernel_execve);

unsigned long arch_align_stack(unsigned long sp)
{
#ifdef CONFIG_ARC_ADDR_SPACE_RND
	/*
	 * ELF loader sets this flag way early.
	 * So no need to check for multiple things like
	 *   !(current->personality & ADDR_NO_RANDOMIZE)
	 *   randomize_va_space
	 */
	if (current->flags & PF_RANDOMIZE) {

		/* Stack grows down for ARC */
		sp -= get_random_int() & ~PAGE_MASK;
	}
#endif

	/* always align stack to 16 bytes */
	sp &= ~0xF;

	return sp;
}

int elf_check_arch(const struct elf32_hdr *x)
{
	unsigned int eflags;

	if (x->e_machine != EM_ARCOMPACT)
		return 0;

	/*
	 * Although asm-generic-unistd is etched in stone for us now (will
	 * _always_ be defined), #ifdef makes intentions clearer
	 */
#ifdef _ASM_GENERIC_UNISTD_H
	eflags = x->e_flags;
	if ((eflags & EF_ARC_OSABI_MSK) < EF_ARC_OSABI_V2) {
		pr_err("ABI mismatch - you need newer toolchain\n");
		force_sigsegv(SIGSEGV, current);
		return 0;
	}
#endif

	return 1;
}
EXPORT_SYMBOL(elf_check_arch);
