# Main application component
# Uses the yaffs component for filesystem support
# Includes the W25N NAND driver specific to this project

idf_component_register(
    SRCS 
        "main.c" 
        "w25n_yaffs_drv.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        yaffs 
        driver
        esp_timer
)
