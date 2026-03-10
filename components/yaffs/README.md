# YAFFS Filesystem Component for ESP-IDF

[YAFFS](https://yaffs.net/) (Yet Another Flash File System) is a filesystem designed for raw NAND flash. This component packages the YAFFS direct interface for ESP-IDF projects and leaves the NAND driver implementation to the application.

## Scope

This component provides:

- YAFFS core sources
- ESP-IDF/FreeRTOS OS glue
- public headers for the YAFFS direct interface

This component does not provide:

- a generic NAND driver
- partition table integration
- automatic flash geometry discovery
- board-specific pin configuration

## Supported Environment

- ESP-IDF 5.x or newer
- raw NAND devices with an application-supplied YAFFS driver

Tested in this repo with:

- ESP32-S3
- Winbond W25N04KV SPI NAND
- an external application-level driver

## Installation

Add the component to your project's `components/` directory, or publish it through the ESP-IDF component manager and add it as a dependency.

## Quick Start

```c
#include "yaffsfs.h"
#include "my_nand_driver.h"

void app_main(void)
{
    struct yaffs_dev *dev;

    yaffsfs_OSInitialisation();

    dev = my_nand_create_device("/flash");
    if (!dev) {
        printf("Device creation failed\n");
        return;
    }

    if (yaffs_mount("/flash") < 0) {
        printf("Mount failed: %d\n", yaffsfs_GetLastError());
        return;
    }

    yaffs_unmount("/flash");
}
```

## Driver Contract

Applications must install these driver callbacks in `struct yaffs_dev`:

- `drv_read_chunk_fn`
- `drv_write_chunk_fn`
- `drv_erase_fn`
- `drv_mark_bad_fn`
- `drv_check_bad_fn`
- `drv_initialise_fn`
- `drv_deinitialise_fn`

## Typical Bring-Up Flow

1. Call `yaffsfs_OSInitialisation()` once at startup.
2. Allocate and populate a `struct yaffs_dev`.
3. Set NAND geometry in `dev->param`.
4. Install the low-level driver callbacks in `dev->drv`.
5. Register the device with `yaffs_add_device(dev)`.
6. Mount with `yaffs_mount("/mountpoint")`.

## Configuration

`menuconfig` exposes one component option:

- `YAFFS_ENABLE_TRACE`: keep YAFFS trace output compiled in for debugging

## Notes

- The application owns NAND geometry, bad-block policy, and ECC behavior.
- Board-specific NAND drivers should live in the application or in a separate example repo.
- This port accepts non-`NULL` pointers in `yaffsfs_CheckMemRegion()` and relies on real accesses to fail if a pointer is invalid. That matches typical ESP-IDF direct-interface usage where path strings may live in flash-mapped memory.

## License

This packaged component is distributed under the terms of GPL-2.0. See [LICENSE](LICENSE).

YAFFS is also available commercially from Aleph One Ltd.

## Resources

- [YAFFS Official Documentation](https://yaffs.net/documents/)
- [YAFFS Direct Interface Guide](https://yaffs.net/documents/yaffs-direct-interface/)
