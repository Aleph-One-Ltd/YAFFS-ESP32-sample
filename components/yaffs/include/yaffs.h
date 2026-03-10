/*
 * YAFFS Filesystem for ESP-IDF - Main Header
 *
 * This is a convenience header that includes all commonly needed
 * YAFFS headers for typical application use.
 *
 * Quick Start:
 * 1. Call yaffsfs_OSInitialisation() once at startup
 * 2. Create a yaffs_dev structure with your NAND driver
 * 3. Call yaffs_add_device() to register it
 * 4. Call yaffs_mount("/your_mount_point")
 * 5. Use yaffs_open(), yaffs_read(), yaffs_write(), etc.
 *
 * Applications must provide their own NAND driver implementation.
 *
 * YAFFS is dual-licensed:
 * - GPL v2 for open source projects
 * - Commercial license available from Aleph One Ltd
 *   https://yaffs.net/
 */

#ifndef YAFFS_H
#define YAFFS_H

/* Core filesystem API */
#include "yaffsfs.h"

/* Device and driver structures */
#include "yaffs_guts.h"

/* OS glue layer (for yaffsfs_OSInitialisation) */
#include "yaffs_osglue.h"

/* Trace/debug definitions */
#include "yaffs_trace.h"

#endif /* YAFFS_H */
