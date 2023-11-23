/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "esp_mmu_map.h"
#include "hal/mmu_types.h"
#include "esp_partition.h"
#include "esp_log.h"
// #include "mmu_types.h"
#include "esp_heap_caps.h"
// #include "esp_private/esp_mmu_map_private.h"
#include "esp_private/cache_utils.h"


static const char *TAG = "example";
#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

void app_main(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    assert(partition != NULL);
    // heap_caps_malloc

    static int store_data = 123123;

    // Prepare data to be read later using the mapped address
    ESP_ERROR_CHECK(esp_partition_erase_range(partition, 0, partition->size));
    ESP_ERROR_CHECK(esp_partition_write(partition, 0, &store_data, sizeof(int)));
    ESP_LOGI(TAG, "Written sample data to partition: %d", store_data);

    const void *map_ptr;
    esp_partition_mmap_handle_t map_handle;

    // Map the partition to data memory
    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, sizeof(int), ESP_PARTITION_MMAP_DATA, &map_ptr, &map_handle));
    ESP_LOGI(TAG, "Mapped partition to data memory address %p", map_ptr);

    // Read back the written verification data using the mapped memory pointer
    int read_data;
    // memcpy(read_data, map_ptr, sizeof(read_data));
    read_data = *(int *)map_ptr;
    ESP_LOGI(TAG, "Read sample data from partition using mapped memory: %d", read_data);

    assert(store_data == read_data);
    ESP_LOGI(TAG, "Data matches");

    // Unmap mapped memory
    esp_partition_munmap(map_handle);
    ESP_LOGI(TAG, "Unmapped partition from data memory");

    ESP_LOGI(TAG, "Example end");

    ESP_LOGW("CRITICAL", "s_do_mappingを利用するためにesp_mmu_map.hとesp_mmu_map.cに変化を加えている!!");
    ESP_LOGI("TEST", "%d\n", ALIGN_UP_BY(1000, CONFIG_MMU_PAGE_SIZE));
    s_do_mapping_pointer(MMU_TARGET_FLASH0, 0x3f420000, 0x00110000, ALIGN_UP_BY(1, CONFIG_MMU_PAGE_SIZE));
    int *num = 0x3f420000;
    printf("%d\n", *num);
    // esp_mmu_map_dump_mapped_blocks_private();
    // esp_mmu_map_init();
}
