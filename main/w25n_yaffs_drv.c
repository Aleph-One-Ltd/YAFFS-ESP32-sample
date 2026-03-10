
/*
 * W25N04KV SPI NAND Flash Driver for YAFFS
 *
 * This file provides the YAFFS driver interface for the Winbond W25N04KV
 * SPI NAND flash chip on ESP32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/unistd.h>
#include "sdkconfig.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "yportenv.h"
#include "w25n_yaffs_drv.h"
#include "yaffs_guts.h"
#include "yaffs_trace.h"
#include "yaffsfs.h"

static const char *TAG = "w25n_drv";

/*
 * Newer ESP-IDF versions use SPI2_HOST/SPI3_HOST instead of the legacy
 * HSPI_HOST/VSPI_HOST aliases.
 */
#if CONFIG_IDF_TARGET_ESP32 && defined(VSPI_HOST)
#define W25N_SPI_HOST VSPI_HOST
#elif CONFIG_IDF_TARGET_ESP32
#define W25N_SPI_HOST SPI3_HOST
#else
#define W25N_SPI_HOST SPI2_HOST
#endif

/* W25N04KV Commands */
#define W25N_CMD_RESET              0xFF
#define W25N_CMD_JEDEC_ID           0x9F
#define W25N_CMD_READ_STATUS        0x0F  /* Get Feature */
#define W25N_CMD_WRITE_STATUS       0x1F  /* Set Feature */
#define W25N_CMD_WRITE_ENABLE       0x06
#define W25N_CMD_WRITE_DISABLE      0x04
#define W25N_CMD_BLOCK_ERASE        0xD8
#define W25N_CMD_PROGRAM_LOAD       0x02  /* Load data into buffer */
#define W25N_CMD_RANDOM_PROGRAM_LOAD 0x84 /* Load data at arbitrary column */
#define W25N_CMD_PROGRAM_EXECUTE    0x10  /* Program buffer to page */
#define W25N_CMD_PAGE_DATA_READ     0x13  /* Read page to buffer */
#define W25N_CMD_READ_DATA          0x03  /* Read from buffer */

/* W25N04KV Register Addresses */
#define W25N_REG_PROTECTION     0xA0
#define W25N_REG_CONFIG         0xB0
#define W25N_REG_STATUS         0xC0

/* Status register bits */
#define W25N_STATUS_BUSY        0x01
#define W25N_STATUS_WEL         0x02
#define W25N_STATUS_EFAIL       0x04
#define W25N_STATUS_PFAIL       0x08

struct w25n_context {
    spi_device_handle_t spi_handle;
    int cs_gpio;
    int chip_index;
    int initialized;
};

static SemaphoreHandle_t w25n_bus_mutex;
static int w25n_bus_initialized;

static const char *w25n_chip_name(const struct w25n_context *ctx)
{
    return (ctx && ctx->chip_index == 1) ? "flash1" : "flash0";
}

static struct w25n_context *w25n_get_context(struct yaffs_dev *dev)
{
    return dev ? (struct w25n_context *)dev->driver_context : NULL;
}

static void w25n_delay_us(uint32_t delay_us)
{
    esp_rom_delay_us(delay_us);
}

static void w25n_delay_ms(uint32_t delay_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static void w25n_configure_output_gpio(int gpio, int level)
{
    if (gpio < 0) return;

    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, level);
}

static int w25n_buffer_is_erased(const uint8_t *buffer, int len)
{
    int i;

    if (!buffer || len <= 0) return 1;

    for (i = 0; i < len; i++) {
        if (buffer[i] != 0xFF) return 0;
    }

    return 1;
}

static int w25n_bus_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = W25N_GPIO_MOSI,
        .miso_io_num = W25N_GPIO_MISO,
        .sclk_io_num = W25N_GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
    esp_err_t ret;

    if (w25n_bus_initialized) return YAFFS_OK;

    w25n_configure_output_gpio(W25N_GPIO_CS0, 1);
    w25n_configure_output_gpio(W25N_GPIO_CS1, 1);
    w25n_configure_output_gpio(W25N_GPIO_WP, 1);
    w25n_configure_output_gpio(W25N_GPIO_HOLD, 1);

    ret = spi_bus_initialize(W25N_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return YAFFS_FAIL;
    }

    w25n_bus_mutex = xSemaphoreCreateMutex();
    if (!w25n_bus_mutex) {
        ESP_LOGE(TAG, "Failed to create SPI bus mutex");
        return YAFFS_FAIL;
    }

    w25n_bus_initialized = 1;
    return YAFFS_OK;
}

static void w25n_bus_lock(void)
{
    if (w25n_bus_mutex) xSemaphoreTake(w25n_bus_mutex, portMAX_DELAY);
}

static void w25n_bus_unlock(void)
{
    if (w25n_bus_mutex) xSemaphoreGive(w25n_bus_mutex);
}

/*----------------------------------------------------------------------------
 * Low-level SPI functions
 *----------------------------------------------------------------------------*/

static void w25n_spi_transmit(struct w25n_context *ctx, const uint8_t *data, int bytes)
{
    spi_transaction_t trans = {
        .tx_buffer = data,
        .length = bytes * 8,
        .rxlength = 0
    };
    spi_device_transmit(ctx->spi_handle, &trans);
}

static void w25n_spi_receive(struct w25n_context *ctx, uint8_t cmd, uint32_t addr,
                             int addr_len, int dummy_bits, uint8_t *buffer, int len)
{
    uint8_t temp_buffer[len + 1];

    memset(temp_buffer, 0, sizeof(temp_buffer));

    spi_transaction_ext_t trans_ext = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_CMD,
            .cmd = cmd,
            .addr = addr,
            .rx_buffer = temp_buffer,
            .rxlength = (len + 1) * 8,
            .length = 0
        },
        .address_bits = addr_len * 8,
        .command_bits = 8,
        .dummy_bits = dummy_bits
    };

    if (dummy_bits > 0) trans_ext.base.flags |= SPI_TRANS_VARIABLE_DUMMY;

    spi_device_transmit(ctx->spi_handle, (spi_transaction_t *)&trans_ext);
    memcpy(buffer, temp_buffer + 1, len);
}

static void w25n_send_command(struct w25n_context *ctx, uint8_t cmd)
{
    spi_transaction_ext_t trans_ext = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_CMD,
            .cmd = cmd,
            .length = 0,
            .rxlength = 0
        },
        .command_bits = 8,
        .address_bits = 0
    };
    spi_device_transmit(ctx->spi_handle, (spi_transaction_t *)&trans_ext);
}

/*----------------------------------------------------------------------------
 * W25N04KV specific functions
 *----------------------------------------------------------------------------*/

static uint8_t w25n_read_register(struct w25n_context *ctx, uint8_t reg_addr)
{
    uint8_t raw[2] = {0};
    
    spi_transaction_ext_t trans_ext = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR,
            .cmd = W25N_CMD_READ_STATUS,
            .addr = reg_addr,
            .rx_buffer = raw,
            .rxlength = 16,
            .length = 0
        },
        .command_bits = 8,
        .address_bits = 8
    };
    
    spi_device_transmit(ctx->spi_handle, (spi_transaction_t *)&trans_ext);
    return raw[1];
}

static int w25n_wait_ready(struct w25n_context *ctx, int timeout_ms);

static int w25n_wait_register_mask_us(struct w25n_context *ctx, uint8_t reg_addr,
                                      uint8_t mask, uint8_t value, uint32_t timeout_us)
{
    while (timeout_us > 0) {
        if ((w25n_read_register(ctx, reg_addr) & mask) == value) return YAFFS_OK;

        if (timeout_us <= 10) break;

        w25n_delay_us(10);
        timeout_us -= 10;
    }

    return YAFFS_FAIL;
}

static void w25n_write_register(struct w25n_context *ctx, uint8_t reg_addr, uint8_t value)
{
    uint8_t cmd[3] = {W25N_CMD_WRITE_STATUS, reg_addr, value};
    w25n_spi_transmit(ctx, cmd, 3);
    w25n_wait_ready(ctx, 10);
}

static void w25n_write_enable(struct w25n_context *ctx)
{
    w25n_send_command(ctx, W25N_CMD_WRITE_ENABLE);
    w25n_wait_register_mask_us(ctx, W25N_REG_STATUS, W25N_STATUS_WEL, W25N_STATUS_WEL, 1000);
}

static int w25n_wait_ready(struct w25n_context *ctx, int timeout_ms)
{
    int timeout = timeout_ms;
    
    do {
        uint8_t status = w25n_read_register(ctx, W25N_REG_STATUS);
        if (!(status & W25N_STATUS_BUSY)) return YAFFS_OK;
        w25n_delay_ms(1);
        timeout--;
    } while (timeout > 0);
    
    return YAFFS_FAIL;
}

static void w25n_disable_protection(struct w25n_context *ctx)
{
    uint8_t prot = w25n_read_register(ctx, W25N_REG_PROTECTION);
    if (prot & 0x7C) {
        w25n_write_enable(ctx);
        w25n_write_register(ctx, W25N_REG_PROTECTION, 0x00);
        if (w25n_read_register(ctx, W25N_REG_PROTECTION) & 0x7C) {
            ESP_LOGW(TAG, "%s: protection bits still set after clear",
                     w25n_chip_name(ctx));
        }
    }
}

/*----------------------------------------------------------------------------
 * Page read/write operations for W25N04KV
 * 
 * W25N04KV has a 2-step read process:
 *   1. Page Data Read (0x13) - loads page from NAND array to internal buffer
 *   2. Read Data (0x03) - reads data from internal buffer
 *
 * W25N04KV has a 2-step write process:
 *   1. Program Load (0x02) - loads data into internal buffer
 *   2. Program Execute (0x10) - programs buffer contents to NAND page
 *----------------------------------------------------------------------------*/

static int w25n_read_page_locked(struct w25n_context *ctx, int page_addr,
                                 uint8_t *data, int data_len,
                                 uint8_t *spare, int spare_len)
{
    /* Step 1: Issue Page Data Read command to load page into buffer */
    uint8_t cmd[4] = {
        W25N_CMD_PAGE_DATA_READ,
        0x00,  /* Dummy byte */
        (page_addr >> 8) & 0xFF,
        page_addr & 0xFF
    };
    w25n_spi_transmit(ctx, cmd, 4);
    
    /* Wait for page to be loaded into buffer */
    if (w25n_wait_ready(ctx, 100) != YAFFS_OK) {
        ESP_LOGE(TAG, "%s: timeout reading page %d", w25n_chip_name(ctx), page_addr);
        return YAFFS_FAIL;
    }
    
    /* Step 2: Read data from buffer */
    if (data && data_len > 0) {
        /* Read from column 0 (data area) */
        w25n_spi_receive(ctx, W25N_CMD_READ_DATA, 0, 2, 0, data, data_len);
    }
    
    if (spare && spare_len > 0) {
        /* Read from column 2048 (spare area) */
        w25n_spi_receive(ctx, W25N_CMD_READ_DATA, W25N_PAGE_SIZE, 2, 0, spare, spare_len);
    }
    
    return YAFFS_OK;
}

static int w25n_write_page_locked(struct w25n_context *ctx, int page_addr,
                                  const uint8_t *data, int data_len,
                                  const uint8_t *spare, int spare_len)
{
    /* Step 1: Load data into buffer with Program Load command */
    if (data && data_len > 0) {
        w25n_write_enable(ctx);
        
        /* Create command + column address + data */
        uint8_t *tx_buf = malloc(3 + data_len);
        if (!tx_buf) return YAFFS_FAIL;
        
        tx_buf[0] = W25N_CMD_PROGRAM_LOAD;
        tx_buf[1] = 0x00;  /* Column address high */
        tx_buf[2] = 0x00;  /* Column address low (start at 0) */
        memcpy(tx_buf + 3, data, data_len);
        
        w25n_spi_transmit(ctx, tx_buf, 3 + data_len);
        free(tx_buf);
    }
    
    if (spare && spare_len > 0) {
        w25n_write_enable(ctx);
        
        /* Load spare data at column 2048 using Random Program Load */
        uint8_t *tx_buf = malloc(3 + spare_len);
        if (!tx_buf) return YAFFS_FAIL;
        
        tx_buf[0] = W25N_CMD_RANDOM_PROGRAM_LOAD;
        tx_buf[1] = (W25N_PAGE_SIZE >> 8) & 0xFF;
        tx_buf[2] = W25N_PAGE_SIZE & 0xFF;
        memcpy(tx_buf + 3, spare, spare_len);
        
        w25n_spi_transmit(ctx, tx_buf, 3 + spare_len);
        free(tx_buf);
    }
    
    /* Step 2: Program Execute - write buffer to NAND page */
    w25n_write_enable(ctx);
    
    uint8_t exec_cmd[4] = {
        W25N_CMD_PROGRAM_EXECUTE,
        0x00,  /* Dummy */
        (page_addr >> 8) & 0xFF,
        page_addr & 0xFF
    };
    w25n_spi_transmit(ctx, exec_cmd, 4);
    
    /* Wait for program to complete */
    if (w25n_wait_ready(ctx, 1000) != YAFFS_OK) {
        ESP_LOGE(TAG, "%s: timeout programming page %d", w25n_chip_name(ctx), page_addr);
        return YAFFS_FAIL;
    }
    
    /* Check for program failure */
    uint8_t status = w25n_read_register(ctx, W25N_REG_STATUS);
    if (status & W25N_STATUS_PFAIL) {
        ESP_LOGE(TAG, "%s: program failed for page %d", w25n_chip_name(ctx), page_addr);
        return YAFFS_FAIL;
    }
    
    return YAFFS_OK;
}

/*----------------------------------------------------------------------------
 * YAFFS Driver Interface Functions
 *----------------------------------------------------------------------------*/

static int w25n_drv_write_chunk(struct yaffs_dev *dev, int nand_chunk,
                                const u8 *data, int data_len,
                                const u8 *oob, int oob_len)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    int ret;
    
    if (!ctx || (!data && !oob)) return YAFFS_FAIL;

    w25n_bus_lock();
    ret = w25n_write_page_locked(ctx, nand_chunk, data, data_len, oob, oob_len);
    w25n_bus_unlock();

    return ret;
}

static int w25n_drv_read_chunk(struct yaffs_dev *dev, int nand_chunk,
                               u8 *data, int data_len,
                               u8 *oob, int oob_len,
                               enum yaffs_ecc_result *ecc_result)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    int ret;

    if (!ctx) return YAFFS_FAIL;
    
    w25n_bus_lock();
    ret = w25n_read_page_locked(ctx, nand_chunk, data, data_len, oob, oob_len);
    
    /* W25N04KV has internal ECC - check ECC status from status register */
    if (ret == YAFFS_OK && ecc_result) {
        uint8_t status = w25n_read_register(ctx, W25N_REG_STATUS);
        uint8_t ecc_bits = (status >> 4) & 0x03;
        
        switch (ecc_bits) {
            case 0: *ecc_result = YAFFS_ECC_RESULT_NO_ERROR; break;
            case 1: *ecc_result = YAFFS_ECC_RESULT_FIXED; break;
            case 2: *ecc_result = YAFFS_ECC_RESULT_UNFIXED; break;
            case 3: *ecc_result = YAFFS_ECC_RESULT_FIXED; break;
        }

        if (*ecc_result == YAFFS_ECC_RESULT_UNFIXED &&
            w25n_buffer_is_erased(data, data_len) &&
            w25n_buffer_is_erased(oob, oob_len)) {
            *ecc_result = YAFFS_ECC_RESULT_NO_ERROR;
        }

    }
    w25n_bus_unlock();
    
    return ret;
}

static int w25n_drv_erase_block(struct yaffs_dev *dev, int block_no)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    int page_addr;
    uint8_t cmd[4];
    int ret;
    
    if (!ctx) return YAFFS_FAIL;

    page_addr = block_no * W25N_PAGES_PER_BLOCK;
    cmd[0] = W25N_CMD_BLOCK_ERASE;
    cmd[1] = 0x00;
    cmd[2] = (page_addr >> 8) & 0xFF;
    cmd[3] = page_addr & 0xFF;

    w25n_bus_lock();
    w25n_write_enable(ctx);
    w25n_spi_transmit(ctx, cmd, 4);

    ret = w25n_wait_ready(ctx, 2000);
    if (ret != YAFFS_OK) {
        ESP_LOGE(TAG, "%s: timeout erasing block %d", w25n_chip_name(ctx), block_no);
        w25n_bus_unlock();
        return YAFFS_FAIL;
    }

    {
        uint8_t status = w25n_read_register(ctx, W25N_REG_STATUS);
        if (status & W25N_STATUS_EFAIL) {
            ESP_LOGE(TAG, "%s: erase failed for block %d", w25n_chip_name(ctx), block_no);
            w25n_bus_unlock();
            return YAFFS_FAIL;
        }
    }
    w25n_bus_unlock();

    return YAFFS_OK;
}

static int w25n_drv_mark_bad(struct yaffs_dev *dev, int block_no)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    int page_addr;
    uint8_t bad_marker[2] = {0x00, 0x00};
    int ret;

    if (!ctx) return YAFFS_FAIL;

    page_addr = block_no * W25N_PAGES_PER_BLOCK;

    w25n_bus_lock();
    ret = w25n_write_page_locked(ctx, page_addr, NULL, 0, bad_marker, 2);
    w25n_bus_unlock();

    return ret;
}

static int w25n_drv_check_bad(struct yaffs_dev *dev, int block_no)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    int page_addr;
    uint8_t marker[2] = {0xFF, 0xFF};
    int ret;

    if (!ctx) return YAFFS_FAIL;

    page_addr = block_no * W25N_PAGES_PER_BLOCK;

    w25n_bus_lock();
    ret = w25n_read_page_locked(ctx, page_addr, NULL, 0, marker, 2);
    w25n_bus_unlock();

    if (ret != YAFFS_OK) return YAFFS_FAIL;

    if (marker[0] != 0xFF || marker[1] != 0xFF) return YAFFS_FAIL;

    return YAFFS_OK;
}

static int w25n_drv_initialise(struct yaffs_dev *dev)
{
    struct w25n_context *ctx = w25n_get_context(dev);
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10000000,
        .duty_cycle_pos = 128,
        .mode = 0,
        .spics_io_num = -1,
        .cs_ena_pretrans = 1,
        .cs_ena_posttrans = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 3
    };
    esp_err_t ret;

    if (!ctx) return YAFFS_FAIL;
    if (ctx->initialized) return YAFFS_OK;

    if (w25n_bus_init() != YAFFS_OK) return YAFFS_FAIL;

    devcfg.spics_io_num = ctx->cs_gpio;

    ret = spi_bus_add_device(W25N_SPI_HOST, &devcfg, &ctx->spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: SPI add device failed: %s",
                 w25n_chip_name(ctx), esp_err_to_name(ret));
        return YAFFS_FAIL;
    }

    w25n_bus_lock();
    {
        uint8_t jedec_id[3] = {0};
        uint8_t config;

        w25n_spi_receive(ctx, W25N_CMD_JEDEC_ID, 0, 0, 0, jedec_id, 3);
        ESP_LOGI(TAG, "%s: JEDEC ID: 0x%02X 0x%02X 0x%02X",
                 w25n_chip_name(ctx), jedec_id[0], jedec_id[1], jedec_id[2]);

        if (jedec_id[0] != 0xEF) {
            ESP_LOGE(TAG, "%s: unexpected manufacturer ID", w25n_chip_name(ctx));
            w25n_bus_unlock();
            spi_bus_remove_device(ctx->spi_handle);
            ctx->spi_handle = NULL;
            return YAFFS_FAIL;
        }

        w25n_disable_protection(ctx);

        config = w25n_read_register(ctx, W25N_REG_CONFIG);
        config |= 0x18;
        w25n_write_register(ctx, W25N_REG_CONFIG, config);
    }
    w25n_bus_unlock();

    ctx->initialized = 1;
    ESP_LOGI(TAG, "%s: W25N04KV driver initialized on CS%d",
             w25n_chip_name(ctx), ctx->chip_index);

    return YAFFS_OK;
}

static int w25n_drv_deinitialise(struct yaffs_dev *dev)
{
    struct w25n_context *ctx = w25n_get_context(dev);

    if (!ctx) return YAFFS_FAIL;

    if (ctx->spi_handle) {
        spi_bus_remove_device(ctx->spi_handle);
        ctx->spi_handle = NULL;
    }

    ctx->initialized = 0;
    return YAFFS_OK;
}

/*----------------------------------------------------------------------------
 * Public API
 *----------------------------------------------------------------------------*/

int w25n_yaffs_install_drv(struct yaffs_dev *dev)
{
    struct yaffs_driver *drv = &dev->drv;
    
    drv->drv_write_chunk_fn = w25n_drv_write_chunk;
    drv->drv_read_chunk_fn = w25n_drv_read_chunk;
    drv->drv_erase_fn = w25n_drv_erase_block;
    drv->drv_mark_bad_fn = w25n_drv_mark_bad;
    drv->drv_check_bad_fn = w25n_drv_check_bad;
    drv->drv_initialise_fn = w25n_drv_initialise;
    drv->drv_deinitialise_fn = w25n_drv_deinitialise;
    
    return YAFFS_OK;
}

struct yaffs_dev *w25n_yaffs_create_dev(const char *mount_point,
                                        int cs_gpio,
                                        int start_block,
                                        int end_block,
                                        int n_caches)
{
    struct yaffs_dev *dev;
    struct w25n_context *ctx;
    char *name_copy;
    struct yaffs_param *param;
    
    /* Validate parameters */
    if (end_block <= start_block || end_block >= W25N_TOTAL_BLOCKS) {
        ESP_LOGE(TAG, "Invalid block range: %d-%d", start_block, end_block);
        return NULL;
    }
    
    if (cs_gpio != W25N_GPIO_CS0 && cs_gpio != W25N_GPIO_CS1) {
        ESP_LOGE(TAG, "Unsupported chip select GPIO: %d", cs_gpio);
        return NULL;
    }

    /* Allocate device structure */
    dev = malloc(sizeof(struct yaffs_dev));
    ctx = calloc(1, sizeof(struct w25n_context));
    name_copy = strdup(mount_point);
    
    if (!dev || !ctx || !name_copy) {
        free(dev);
        free(ctx);
        free(name_copy);
        return NULL;
    }
    
    memset(dev, 0, sizeof(*dev));
    ctx->cs_gpio = cs_gpio;
    ctx->chip_index = (cs_gpio == W25N_GPIO_CS1) ? 1 : 0;
    dev->driver_context = ctx;
    
    /* Configure device parameters */
    param = &dev->param;
    
    param->name = name_copy;
    param->total_bytes_per_chunk = W25N_PAGE_SIZE;
    param->spare_bytes_per_chunk = W25N_SPARE_SIZE;
    param->chunks_per_block = W25N_PAGES_PER_BLOCK;
    param->start_block = start_block;
    param->end_block = end_block;
    param->n_reserved_blocks = 5;
    param->n_caches = n_caches;
    param->use_nand_ecc = 1;  /* W25N04KV has internal ECC */
    param->is_yaffs2 = 1;     /* Use Yaffs2 mode */
    param->no_tags_ecc = 1;   /* No ECC on tags - using internal ECC */
    param->stored_endian = 1; /* Little endian */
    
    /* Install the driver */
    if (w25n_yaffs_install_drv(dev) != YAFFS_OK) {
        free(name_copy);
        free(ctx);
        free(dev);
        return NULL;
    }
    
    /* Register with YAFFS */
    yaffs_add_device(dev);
    
    ESP_LOGI(TAG, "Created YAFFS device '%s' on CS%d (blocks %d-%d)",
             mount_point, ctx->chip_index, start_block, end_block);
    
    return dev;
}
