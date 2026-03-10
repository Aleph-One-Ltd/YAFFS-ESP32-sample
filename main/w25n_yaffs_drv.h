/*
 * W25N04KV SPI NAND Flash Driver for YAFFS
 *
 * This file provides the YAFFS driver interface for the Winbond W25N04KV
 * SPI NAND flash chip.
 *
 * W25N04KV Specifications:
 * - 4Gbit (512MB) capacity
 * - 2048 bytes per page (data area)
 * - 64 bytes spare area per page
 * - 64 pages per block
 * - 4096 blocks total
 */

#ifndef __W25N_YAFFS_DRV_H__
#define __W25N_YAFFS_DRV_H__

#include "sdkconfig.h"
#include "yaffs_guts.h"

/* W25N04KV Flash Geometry */
#define W25N_PAGE_SIZE          2048    /* Data bytes per page */
#define W25N_SPARE_SIZE         64      /* Spare bytes per page */
#define W25N_PAGES_PER_BLOCK    64      /* Pages per block */
#define W25N_TOTAL_BLOCKS       4096    /* Total blocks in device */

/* Project-configured GPIO assignments */
#define W25N_GPIO_MOSI          CONFIG_W25N_SPI_MOSI_GPIO
#define W25N_GPIO_MISO          CONFIG_W25N_SPI_MISO_GPIO
#define W25N_GPIO_SCLK          CONFIG_W25N_SPI_SCLK_GPIO
#define W25N_GPIO_CS0           CONFIG_W25N_SPI_CS0_GPIO
#define W25N_GPIO_CS1           CONFIG_W25N_SPI_CS1_GPIO
#define W25N_GPIO_WP            CONFIG_W25N_SPI_WP_GPIO
#define W25N_GPIO_HOLD          CONFIG_W25N_SPI_HOLD_GPIO

/* Function to install the W25N driver into a yaffs_dev structure */
int w25n_yaffs_install_drv(struct yaffs_dev *dev);

/* Function to create and configure a yaffs device for W25N04KV */
struct yaffs_dev *w25n_yaffs_create_dev(const char *mount_point, 
                                         int cs_gpio,
                                         int start_block, 
                                         int end_block,
                                         int n_caches);

#endif /* __W25N_YAFFS_DRV_H__ */
