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
