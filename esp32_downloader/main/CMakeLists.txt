# main 组件：只编译 ota_downloader.c
idf_component_register(SRCS "ota_downloader.c"
                       INCLUDE_DIRS ".")
