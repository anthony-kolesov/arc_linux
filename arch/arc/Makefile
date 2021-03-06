#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

UTS_MACHINE := arc

KBUILD_DEFCONFIG := fpga_defconfig

# Platform directory name (plat-...).
# This list is sorted alphanumerically by CONFIG_* macro name.
platform-$(CONFIG_ARC_PLAT_FPGA_LEGACY)	:= arcfpga
platform-$(CONFIG_ARC_PLAT_HAPS51)	:= haps51
# !! A new platform should be added here.

PLATFORM := $(platform-y)
export PLATFORM

cflags-y	+= -Iarch/arc/plat-$(PLATFORM)/include

LINUXINCLUDE	+=  -include ${src}/arch/arc/include/asm/defines.h \
		    -include ${src}/arch/arc/include/asm/current.h

cflags-y	+= -a7 -fno-common -fno-builtin -fno-short-enums -D__linux__

atleast_gcc44 :=  $(call cc-ifversion, -gt, 0402, y)
cflags-$(atleast_gcc44)			+= -fsection-anchors

cflags-$(CONFIG_ARC_HAS_LLSC)		+=
cflags-$(CONFIG_ARC_HAS_SWAPE)		+= -Xswap -core4
cflags-$(CONFIG_ARC_HAS_RTSC)		+= -Xrtsc -core4
cflags-$(CONFIG_ARC_DW2_UNWIND)		+=

# Generic build system already passes -Os, additonally uninline some code
cflags-$(CONFIG_CC_OPTIMIZE_FOR_SIZE)	+= -DNONINLINE_MEMSET

ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
# Generic build system uses -O2, we want -O3
cflags-y  += -O3
endif

# small data is default for elf32 tool-chain. If not usable, disable it
# This also allows repurposing GP as scratch reg to gcc reg allocator
disable_small_data := y
cflags-$(disable_small_data)		+=-Hnosdata

# Low level event tracing with Metaware debugger assist needs symbol info
cflags-$(CONFIG_ARC_DBG_EVENT_TIMELINE) += -g

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -EB
ldflags-$(CONFIG_CPU_BIG_ENDIAN)	+= -EB

# GNU ld doesn't accept --build-id w/o this workaround
# [[-marclinux]]
ldflags-y				+=
# Don't stop linking if there a multiple definitions of symbols.
ldflags-y += -z dup

ARC_LIBGCC				:= -a7
cflags-$(CONFIG_ARC_HAS_HW_MPY)		+=

ifndef CONFIG_ARC_HAS_HW_MPY
	cflags-y	+= -mno-mpy

# newlib for ARC700 assumes MPY to be always present, which is generally true
# However, if someone really doesn't want MPY, we need to use the 600 ver
# which coupled with -mno-mpy will use mpy emulation
# With gcc 4.4.7, -mno-mpy is enough to make any other related adjustments,
# e.g. increased cost of MPY. With gcc 4.2.1 this had to be explicitly hinted

	ARC_LIBGCC		:= -a6
	ifneq ($(atleast_gcc44),y)
		cflags-y	+= -multcost=30
	endif
endif

LIBGCC	:= $(shell $(CC) $(ARC_LIBGCC) $(cflags-y) --print-libgcc-file-name)

# Don't print copright message and create archive even if there are no input files.
KBUILD_ARFLAGS:=fq

# Modules with short calls might break for calls into builtin-kernel
KBUILD_CFLAGS_MODULE	+= -mlong-calls

# Finally dump eveything into kernel build system
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_AFLAGS	+= $(KBUILD_CFLAGS)
LDFLAGS		+= $(ldflags-y)

# Needed for Linker script preprocessing
KBUILD_CPPFLAGS	+= -Iarch/arc/plat-$(PLATFORM)/include

head-y		:= arch/arc/kernel/head.o arch/arc/kernel/init_task.o

# See arch/arc/Kbuild for content of core part of the kernel
core-y				+= arch/arc/

# w/o this ifneq, make ARCH=arc clean was crapping out
ifneq ($(platform-y),)
core-y				+= arch/arc/plat-$(PLATFORM)/
endif

drivers-$(CONFIG_OPROFILE)	+= arch/arc/oprofile/
drivers-y			+= arch/arc/drivers/
libs-y				+= arch/arc/lib/ $(LIBGCC)

#default target for make without any arguements.
KBUILD_IMAGE := bootpImage

all:	$(KBUILD_IMAGE)
boot	:= arch/arc/boot

bootpImage: vmlinux

uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# Hacks to enable final link due to absence of link-time branch relexation
# and gcc choosing optimal(shorter) branches at -O3
#
# vineetg Feb 2010: -mlong-calls switched off for overall kernel build
# However lib/decompress_inflate.o (.init.text) calls
# zlib_inflate_workspacesize (.text) causing relocation errors.
# Thus forcing all exten calls in this file to be long calls
export CFLAGS_decompress_inflate.o = -mlong-calls

# some debug printk getting out of range from sched code
ifdef CONFIG_SMP
export CFLAGS_sched.o = -mmedium-calls
endif
