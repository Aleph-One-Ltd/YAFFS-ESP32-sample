/*
 * YAFFS OS Glue Layer for ESP-IDF (FreeRTOS)
 *
 * This file provides the OS abstraction functions required by YAFFS
 * for use with ESP-IDF and FreeRTOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "yportenv.h"
#include "yaffs_osglue.h"
#include "yaffs_guts.h"
#include "yaffs_trace.h"

static const char *TAG = "yaffs_os";

static SemaphoreHandle_t yaffs_mutex = NULL;
static unsigned malloc_current = 0;
static unsigned malloc_high_water = 0;

/* Global trace mask variable - controls debug output */
unsigned yaffs_trace_mask = 
    YAFFS_TRACE_MOUNT |
    YAFFS_TRACE_ERROR |
    YAFFS_TRACE_BUG;

/*
 * yaffsfs_SetError - Set the system error code
 */
void yaffsfs_SetError(int err)
{
    errno = err;
}

/*
 * yaffsfs_GetLastError - Get the last error code
 */
int yaffsfs_GetLastError(void)
{
    return errno;
}

/*
 * yaffsfs_Lock - Lock YAFFS for thread-safe access
 * Called by YAFFS before accessing shared data structures
 */
void yaffsfs_Lock(void)
{
    if (yaffs_mutex) {
        xSemaphoreTake(yaffs_mutex, portMAX_DELAY);
    }
}

/*
 * yaffsfs_Unlock - Unlock YAFFS after accessing shared data
 */
void yaffsfs_Unlock(void)
{
    if (yaffs_mutex) {
        xSemaphoreGive(yaffs_mutex);
    }
}

/*
 * yaffsfs_CurrentTime - Get the current time as a UNIX timestamp
 */
unsigned yaffsfs_CurrentTime(void)
{
    time_t t = time(NULL);
    return (unsigned)t;
}

/*
 * yaffsfs_malloc - Allocate memory for YAFFS
 */
void *yaffsfs_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr) {
        malloc_current += size;
        if (malloc_current > malloc_high_water) {
            malloc_high_water = malloc_current;
        }
    }
    return ptr;
}

/*
 * yaffsfs_free - Free memory allocated by YAFFS
 */
void yaffsfs_free(void *ptr)
{
    if (ptr) {
        free(ptr);
        // Note: We can't track exact deallocation size without storing it
    }
}

/*
 * yaffsfs_get_malloc_values - Get memory allocation statistics
 */
void yaffsfs_get_malloc_values(unsigned *current, unsigned *high_water)
{
    if (current)
        *current = malloc_current;
    if (high_water)
        *high_water = malloc_high_water;
}

/*
 * yaffsfs_CheckMemRegion - Check if a memory region is valid
 * Returns 0 if the memory region is valid, -1 otherwise
 */
int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request)
{
    (void)size;
    (void)write_request;

    /* This direct port cannot reliably validate arbitrary pointer ranges.
     * Accept any non-NULL pointer and let the actual access fault if invalid.
     */
    if (!addr)
        return -1;

    return 0;
}

/*
 * yaffsfs_OSInitialisation - Initialize OS resources for YAFFS
 * Must be called before any other YAFFS functions
 */
void yaffsfs_OSInitialisation(void)
{
    if (yaffs_mutex == NULL) {
        yaffs_mutex = xSemaphoreCreateMutex();
        if (yaffs_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create YAFFS mutex!");
        }
    }
    
    malloc_current = 0;
    malloc_high_water = 0;
}

/*
 * yaffs_bug_fn - Report a YAFFS bug
 */
void yaffs_bug_fn(const char *file_name, int line_no)
{
    ESP_LOGE(TAG, "YAFFS BUG at %s:%d", file_name, line_no);
}
