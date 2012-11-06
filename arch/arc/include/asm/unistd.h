/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Being uClibc based we need some of the deprecated syscalls:
 * -Not emulated by uClibc at all
 *	unlink, mkdir,... (needed by Busybox, LTP etc)
 *	times (needed by LTP pan test harness)
 * -Not emulated efficiently
 *	select: emulated using pselect (but extra code to chk usec > 1sec)
 *
 * some (send/recv) correctly emulated using (recfrom/sendto) and
 * some arch specific ones (fork/vfork)can easily be emulated using clone but
 * thats the price of using common-denominator....
 */
#define __ARCH_WANT_SYSCALL_NO_AT
#define __ARCH_WANT_SYSCALL_NO_FLAGS
#define __ARCH_WANT_SYSCALL_OFF_T
#define __ARCH_WANT_SYSCALL_DEPRECATED
#define __ARCH_WANT_KERNEL_EXECVE
#define __ARCH_WANT_SYS_EXECVE

#define sys_mmap2 sys_mmap_pgoff

#include <asm-generic/unistd.h>

#define NR_syscalls	__NR_syscalls

/* ARC specific syscall */
#define __NR_cacheflush		(__NR_arch_specific_syscall + 0)
#define __NR_arc_settls		(__NR_arch_specific_syscall + 1)
#define __NR_arc_gettls		(__NR_arch_specific_syscall + 2)

__SYSCALL(__NR_cacheflush, sys_cacheflush)
__SYSCALL(__NR_arc_settls, sys_arc_settls)
__SYSCALL(__NR_arc_gettls, sys_arc_gettls)


/* Generic syscall (fs/filesystems.c - lost in asm-generic/unistd.h */
#define __NR_sysfs		(__NR_arch_specific_syscall + 3)
__SYSCALL(__NR_sysfs, sys_sysfs)
