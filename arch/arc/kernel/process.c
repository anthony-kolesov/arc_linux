/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Amit Bhor, Kanika Nema: Codito Technologies 2004
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/elf.h>
#include <linux/tick.h>

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

int sys_execve(const char __user *filenamei, const char __user *__user *argv,
	       const char __user *__user *envp, struct pt_regs *regs)
{
	long error;
	struct filename *filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename->name, argv, envp, regs);
	putname(filename);
out:
	return error;
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
		"mov   r8, %1	\n\t"
		"trap0		\n\t"
		: "+r"(filenm_n_ret)
		: "i"(__NR_execve), "r"(arg2), "r"(arg3)
		: "r8", "memory");

	return filenm_n_ret;
}
EXPORT_SYMBOL(kernel_execve);

SYSCALL_DEFINE1(arc_settls, void *, user_tls_data_ptr)
{
	task_thread_info(current)->thr_ptr = (unsigned int)user_tls_data_ptr;
	return 0;
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
SYSCALL_DEFINE0(arc_gettls)
{
	return task_thread_info(current)->thr_ptr;
}

static inline void arch_idle(void)
{
	__asm__("sleep");
}

void cpu_idle(void)
{
	/* Since we SLEEP in idle loop, TIF_POLLING_NRFLAG can't be set */

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_idle_enter();

		while (!need_resched())
			arch_idle();

		tick_nohz_idle_exit();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void) __attribute__((noreturn));

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
		unsigned long usp, unsigned long arg,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *c_regs;        /* child's pt_regs */
	unsigned long *childksp;       /* to unwind out of __switch_to() */
	struct callee_regs *c_callee;  /* child's callee regs */
	struct callee_regs *parent_callee;  /* paren't callee */

	/* Mark the specific anchors to begin with (see pic above) */
	c_regs = task_pt_regs(p);
	childksp = (unsigned long *)c_regs - 2;  /* 2 words for FP/BLINK */
	c_callee = ((struct callee_regs *)childksp) - 1;

	/*
	 * At the end of this function, kernel SP is all set for
	 * switch_to to start unwinding.
	 * For kernel threads we don't have callee regs, but the stack
	 * layout nevertheless needs to remain the same
	 */
	p->thread.ksp = (unsigned long)c_callee;	/* THREAD_KSP */

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(c_regs, 0, sizeof(struct pt_regs));
		c_regs->r0 = arg; /* argument to kernel thread */
		c_regs->r1 = usp;  /* function */
		c_regs->status32 = read_aux_reg(0xa);

		c_regs->sp =
		    (unsigned long)task_thread_info(p) + (THREAD_SIZE - 4);

		/* __switch_to expects FP(0), BLINK(return addr) at top */
		childksp[0] = 0;			/* fp */
		childksp[1] = (unsigned long)ret_from_kernel_thread; /* blink */
		return 0;
	}

	/*--------- User Task Only --------------*/

	/* __switch_to expects FP(0), BLINK(return addr) at top of stack */
	childksp[0] = 0;				/* for POP fp */
	childksp[1] = (unsigned long)ret_from_fork;	/* for POP blink */

	/* Copy parents pt regs on child's kernel mode stack */
	*c_regs = *regs;

	c_regs->sp = usp;
	c_regs->r0 = 0;		/* fork returns 0 in child */

	parent_callee = ((struct callee_regs *)regs) - 1;
	*c_callee = *parent_callee;

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

	return 0;
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

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
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
		blink = *((unsigned int *)regs - 1);

	return blink;
}

int elf_check_arch(const struct elf32_hdr *x)
{
	unsigned int eflags;

	if (x->e_machine != EM_ARCOMPACT)
		return 0;

	eflags = x->e_flags;
	if ((eflags & EF_ARC_OSABI_MSK) < EF_ARC_OSABI_V2) {
		pr_err("ABI mismatch - you need newer toolchain\n");
		force_sigsegv(SIGSEGV, current);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(elf_check_arch);
