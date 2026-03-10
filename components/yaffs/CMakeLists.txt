# YAFFS Filesystem Component for ESP-IDF
#
# This component provides the YAFFS filesystem for use with ESP-IDF projects.
# Users must provide their own NAND driver implementation.

idf_component_register(
    SRCS
        "src/yaffsfs.c"
        "src/yaffs_allocator.c"
        "src/yaffs_attribs.c"
        "src/yaffs_bitmap.c"
        "src/yaffs_cache.c"
        "src/yaffs_checkptrw.c"
        "src/yaffs_ecc.c"
        "src/yaffs_endian.c"
        "src/yaffs_error.c"
        "src/yaffs_guts.c"
        "src/yaffs_hweight.c"
        "src/yaffs_nameval.c"
        "src/yaffs_nand.c"
        "src/yaffs_packedtags1.c"
        "src/yaffs_packedtags2.c"
        "src/yaffs_summary.c"
        "src/yaffs_tagscompat.c"
        "src/yaffs_tagsmarshall.c"
        "src/yaffs_verify.c"
        "src/yaffs_yaffs1.c"
        "src/yaffs_yaffs2.c"
        "port/yaffs_osglue.c"
    INCLUDE_DIRS
        "include"
        "port"
    REQUIRES
        freertos
)

# Required compile definitions for YAFFS direct interface
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_YAFFS_DEFINES_TYPES
    CONFIG_YAFFS_PROVIDE_DEFS
    CONFIG_YAFFSFS_PROVIDE_VALUES
    CONFIG_YAFFS_DIRECT
)

# Compile out YAFFS trace calls unless explicitly enabled in Kconfig.
if(NOT CONFIG_YAFFS_ENABLE_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_YAFFS_NO_TRACE)
endif()

# Suppress warnings in upstream YAFFS code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-return-type
)
