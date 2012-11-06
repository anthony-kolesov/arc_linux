/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_TIMEX_H
#define _ASM_ARC_TIMEX_H

#define CLOCK_TICK_RATE	CONFIG_ARC_PLAT_CLK	/* Underlying HZ */

#include <asm-generic/timex.h>

/* XXX: get_cycles() to be implemented with RTSC insn */

#endif /* _ASM_ARC_TIMEX_H */
