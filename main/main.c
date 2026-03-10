/* W25N04KV YAFFS Integration Example for ESP32
 *
 * This example demonstrates using YAFFS filesystems on two W25N04KV SPI NAND
 * flash devices sharing one SPI bus.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <sys/unistd.h>

#include "yaffsfs.h"
#include "yaffs_osglue.h"
#include "w25n_yaffs_drv.h"
#include "yaffs_trace.h"

static const char *TAG = "main";
static const size_t BENCHMARK_TOTAL_BYTES = 1024 * 1024;
static const size_t BENCHMARK_IO_BYTES = 4096;

static void halt_forever(void)
{
    while (1) {
        sleep(1);
    }
}

static void require_success(int condition, const char *message)
{
    if (!condition) {
        ESP_LOGE(TAG, "%s (error=%d)", message, yaffsfs_GetLastError());
        halt_forever();
    }
}

static void mount_or_format(const char *mount_path)
{
    int ret = yaffs_mount(mount_path);
    if (ret >= 0) {
        ESP_LOGI(TAG, "%s mounted", mount_path);
        return;
    }

    ESP_LOGW(TAG, "%s mount failed, formatting (error=%d)",
             mount_path, yaffsfs_GetLastError());

    ret = yaffs_format(mount_path, 1, 1, 1);
    require_success(ret >= 0, "Format failed");

    ret = yaffs_mount(mount_path);
    require_success(ret >= 0, "Mount after format failed");

    ESP_LOGI(TAG, "%s formatted and mounted", mount_path);
}

static void log_fs_info(const char *mount_path)
{
    int total_space = yaffs_totalspace(mount_path);
    int free_space = yaffs_freespace(mount_path);

    ESP_LOGI(TAG, "%s total=%d free=%d", mount_path, total_space, free_space);
}

static void write_text_file(const char *path, const char *text)
{
    int fd = yaffs_open(path, O_CREAT | O_RDWR | O_TRUNC, S_IREAD | S_IWRITE);
    require_success(fd >= 0, "Failed to open file for write");

    int written = yaffs_write(fd, text, strlen(text));
    yaffs_close(fd);

    require_success(written == (int)strlen(text), "Failed to write full file");
    ESP_LOGI(TAG, "Wrote %d bytes to %s", written, path);
}

static void read_and_check_file(const char *path, const char *expected)
{
    char buffer[256] = {0};
    int fd = yaffs_open(path, O_RDONLY, 0);
    require_success(fd >= 0, "Failed to open file for read");

    int bytes_read = yaffs_read(fd, buffer, sizeof(buffer) - 1);
    yaffs_close(fd);

    require_success(bytes_read >= 0, "Read failed");
    require_success(strcmp(buffer, expected) == 0, "Read-back data mismatch");

    ESP_LOGI(TAG, "Read %d bytes from %s", bytes_read, path);
}

static void copy_file(const char *src_path, const char *dst_path)
{
    char buffer[256];
    int src_fd = yaffs_open(src_path, O_RDONLY, 0);
    require_success(src_fd >= 0, "Failed to open source file");

    int dst_fd = yaffs_open(dst_path, O_CREAT | O_RDWR | O_TRUNC, S_IREAD | S_IWRITE);
    require_success(dst_fd >= 0, "Failed to open destination file");

    while (1) {
        int bytes_read = yaffs_read(src_fd, buffer, sizeof(buffer));
        require_success(bytes_read >= 0, "Copy read failed");
        if (bytes_read == 0) break;

        int bytes_written = yaffs_write(dst_fd, buffer, bytes_read);
        require_success(bytes_written == bytes_read, "Copy write failed");
    }

    yaffs_close(src_fd);
    yaffs_close(dst_fd);

    ESP_LOGI(TAG, "Copied %s -> %s", src_path, dst_path);
}

static void delete_file(const char *path)
{
    int ret = yaffs_unlink(path);
    require_success(ret == 0, "Delete failed");
    ESP_LOGI(TAG, "Deleted %s", path);
}

static void list_directory(const char *mount_path)
{
    yaffs_DIR *dir = yaffs_opendir(mount_path);
    require_success(dir != NULL, "Failed to open directory");

    ESP_LOGI(TAG, "Directory listing for %s:", mount_path);

    while (1) {
        struct yaffs_dirent *de = yaffs_readdir(dir);
        if (!de) break;

        {
            struct yaffs_stat st;
            char path[280];
            snprintf(path, sizeof(path), "%s/%s", mount_path, de->d_name);
            yaffs_stat(path, &st);
            ESP_LOGI(TAG, "  %s (%d bytes)", de->d_name, (int)st.st_size);
        }
    }

    yaffs_closedir(dir);
}

static void fill_benchmark_pattern(uint8_t *buffer, size_t len, uint32_t seed)
{
    size_t i;

    for (i = 0; i < len; i++) {
        buffer[i] = (uint8_t)((seed + i) & 0xFF);
    }
}

static double benchmark_mib_per_second(size_t bytes, int64_t elapsed_us)
{
    if (elapsed_us <= 0)
        return 0.0;

    return ((double)bytes * 1000000.0) / ((double)elapsed_us * 1024.0 * 1024.0);
}

static void benchmark_partition(const char *mount_path)
{
    static uint8_t write_buffer[4096];
    static uint8_t read_buffer[4096];
    char path[280];
    size_t offset;
    int fd;
    int64_t write_start_us;
    int64_t write_end_us;
    int64_t read_start_us;
    int64_t read_end_us;
    double write_mib_per_sec;
    double read_mib_per_sec;

    snprintf(path, sizeof(path), "%s/benchmark.bin", mount_path);

    fd = yaffs_open(path, O_CREAT | O_RDWR | O_TRUNC, S_IREAD | S_IWRITE);
    require_success(fd >= 0, "Failed to open benchmark file for write");

    write_start_us = esp_timer_get_time();
    for (offset = 0; offset < BENCHMARK_TOTAL_BYTES; offset += BENCHMARK_IO_BYTES) {
        fill_benchmark_pattern(write_buffer, sizeof(write_buffer), (uint32_t)offset);
        require_success(
            yaffs_write(fd, write_buffer, sizeof(write_buffer)) == (int)sizeof(write_buffer),
            "Benchmark write failed"
        );
    }
    require_success(yaffs_fsync(fd) == 0, "Benchmark fsync failed");
    write_end_us = esp_timer_get_time();
    yaffs_close(fd);

    fd = yaffs_open(path, O_RDONLY, 0);
    require_success(fd >= 0, "Failed to open benchmark file for read");

    read_start_us = esp_timer_get_time();
    for (offset = 0; offset < BENCHMARK_TOTAL_BYTES; offset += BENCHMARK_IO_BYTES) {
        fill_benchmark_pattern(write_buffer, sizeof(write_buffer), (uint32_t)offset);
        require_success(
            yaffs_read(fd, read_buffer, sizeof(read_buffer)) == (int)sizeof(read_buffer),
            "Benchmark read failed"
        );
        require_success(
            memcmp(read_buffer, write_buffer, sizeof(read_buffer)) == 0,
            "Benchmark read-back mismatch"
        );
    }
    read_end_us = esp_timer_get_time();
    yaffs_close(fd);

    write_mib_per_sec = benchmark_mib_per_second(BENCHMARK_TOTAL_BYTES, write_end_us - write_start_us);
    read_mib_per_sec = benchmark_mib_per_second(BENCHMARK_TOTAL_BYTES, read_end_us - read_start_us);

    ESP_LOGI(TAG, "%s benchmark write: 1 MiB in %.3f s (%.2f MiB/s)",
             mount_path,
             (double)(write_end_us - write_start_us) / 1000000.0,
             write_mib_per_sec);
    ESP_LOGI(TAG, "%s benchmark read: 1 MiB in %.3f s (%.2f MiB/s)",
             mount_path,
             (double)(read_end_us - read_start_us) / 1000000.0,
             read_mib_per_sec);

    delete_file(path);
}

void app_main(void)
{
    static const char *flash0_src = "/flash0/flash0.txt";
    static const char *flash1_src = "/flash1/flash1.txt";
    static const char *flash0_copy = "/flash0/from_flash1.txt";
    static const char *flash1_copy = "/flash1/from_flash0.txt";
    static const char *flash0_text = "Hello from flash0 on ESP32 YAFFS.\n";
    static const char *flash1_text = "Hello from flash1 on ESP32 YAFFS.\n";
    struct yaffs_dev *flash0_dev;
    struct yaffs_dev *flash1_dev;

    printf("\n=== Dual W25N04KV YAFFS Test ===\n");

    yaffsfs_OSInitialisation();

    flash0_dev = w25n_yaffs_create_dev(
        "/flash0",
        W25N_GPIO_CS0,
        0,
        4095,
        10
    );

    flash1_dev = w25n_yaffs_create_dev(
        "/flash1",
        W25N_GPIO_CS1,
        0,
        4095,
        10
    );

    require_success(flash0_dev != NULL, "Failed to create /flash0 device");
    require_success(flash1_dev != NULL, "Failed to create /flash1 device");

    mount_or_format("/flash0");
    mount_or_format("/flash1");

    log_fs_info("/flash0");
    log_fs_info("/flash1");

    write_text_file(flash0_src, flash0_text);
    write_text_file(flash1_src, flash1_text);

    read_and_check_file(flash0_src, flash0_text);
    read_and_check_file(flash1_src, flash1_text);

    copy_file(flash0_src, flash1_copy);
    copy_file(flash1_src, flash0_copy);

    read_and_check_file(flash1_copy, flash0_text);
    read_and_check_file(flash0_copy, flash1_text);

    benchmark_partition("/flash0");
    benchmark_partition("/flash1");

    list_directory("/flash0");
    list_directory("/flash1");

    delete_file(flash0_src);
    delete_file(flash1_src);
    delete_file(flash0_copy);
    delete_file(flash1_copy);

    list_directory("/flash0");
    list_directory("/flash1");

    yaffs_sync("/flash0");
    yaffs_sync("/flash1");

    printf("\n=== Dual-Chip Test Complete ===\n");

    while (1) {
        sleep(2);
    }
}
