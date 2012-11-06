/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_IO_H
#define _ASM_ARC_IO_H

#include <asm/byteorder.h>
#include <asm/page.h>

extern void __iomem *ioremap(unsigned long physaddr, unsigned long size);
extern void iounmap(const void __iomem *addr);

#define ioremap_nocache(phy, sz)	ioremap(phy, sz)
#define ioremap_wc(phy, sz)		ioremap(phy, sz)

/* Change struct page to physical address */
#define page_to_phys(page)		(page_to_pfn(page) << PAGE_SHIFT)

#include <asm-generic/io.h>

#endif /* _ASM_ARC_IO_H */
