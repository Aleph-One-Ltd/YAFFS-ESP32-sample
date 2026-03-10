/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

/*
 * ydirectenv.h: Environment wrappers for YAFFS direct.
 */

#ifndef __YDIRECTENV_H__
#define __YDIRECTENV_H__

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Type definitions for ESP-IDF/newlib - must come before other yaffs headers */
#ifndef loff_t
typedef long long loff_t;
#endif

#ifndef Y_LOFF_T
#define Y_LOFF_T loff_t
#endif

#ifndef Y_OFF_T
#define Y_OFF_T off_t
#endif

/* Directory entry types if not defined */
#ifndef DT_UNKNOWN
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14
#endif

#include "yaffs_hweight.h"

void yaffs_bug_fn(const char *file_name, int line_no);

#define BUG() do { yaffs_bug_fn(__FILE__, __LINE__); } while (0)

#ifdef CONFIG_YAFFS_USE_32_BIT_TIME_T
	#define YTIME_T u32
#else
	#define YTIME_T u64
#endif

#define YCHAR char
#define YUCHAR unsigned char
#define _Y(x) x

/* Some RTOSs (eg. VxWorks) need strnlen. */
size_t strnlen(const char *s, size_t maxlen);

#define yaffs_strcat(a, b)	strcat(a, b)
#define yaffs_strcpy(a, b)	strcpy(a, b)
#define yaffs_strncpy(a, b, c)	strncpy(a, b, c)
#define yaffs_strnlen(s, m)	strnlen(s, m)
#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
#define yaffs_strcmp(a, b)	strcasecmp(a, b)
#define yaffs_strncmp(a, b, c)	strncasecmp(a, b, c)
#else
#define yaffs_strcmp(a, b)	strcmp(a, b)
#define yaffs_strncmp(a, b, c)	strncmp(a, b, c)
#endif

#define hweight8(x)	yaffs_hweight8(x)
#define hweight32(x)	yaffs_hweight32(x)

#define sort(base, n, sz, cmp_fn, swp) qsort(base, n, sz, cmp_fn)

#define YAFFS_PATH_DIVIDERS  "/"

#ifdef NO_inline
#define inline
#else
#define inline __inline__
#endif

/* Forward declarations for OS glue functions - defined in yaffs_osglue.c */
void *yaffsfs_malloc(size_t size);
void yaffsfs_free(void *ptr);
void yaffsfs_SetError(int err);
void yaffsfs_Lock(void);
void yaffsfs_Unlock(void);
unsigned yaffsfs_CurrentTime(void);
int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request);

#define kmalloc(x, flags) yaffsfs_malloc(x)
#define kfree(x)   yaffsfs_free(x)
#define vmalloc(x) yaffsfs_malloc(x)
#define vfree(x) yaffsfs_free(x)

#define cond_resched()  do {} while (0)

#ifdef CONFIG_YAFFS_NO_TRACE
#define yaffs_trace(...) do { } while (0)
#else
#define yaffs_trace(msk, fmt, ...) do { \
	if (yaffs_trace_mask & (msk)) \
		printf("yaffs: " fmt "\n", ##__VA_ARGS__); \
} while (0)

#endif

#define YAFFS_LOSTNFOUND_NAME		"lost+found"
#define YAFFS_LOSTNFOUND_PREFIX		"obj"

#include "yaffscfg.h"

#define Y_CURRENT_TIME yaffsfs_CurrentTime()
#define Y_TIME_CONVERT(x) x

#define YAFFS_ROOT_MODE			0755
#define YAFFS_LOSTNFOUND_MODE		0700

#include "yaffs_list.h"

#include "yaffsfs.h"

#endif


