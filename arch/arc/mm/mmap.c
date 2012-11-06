/* mmap for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/syscalls.h>

/* Gets dragged in due to __ARCH_WANT_SYSCALL_OFF_T */
SYSCALL_DEFINE6(mmap, unsigned long, addr_hint, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, off)
{
	pr_err("old mmap not supported\n");
	return -EINVAL;
}
