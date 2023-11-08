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
#include "esp_partition.h"
#include "esp_log.h"
// #include "mmu_types.h"
#include "esp_heap_caps.h"

static const char *TAG = "example";

void app_main(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage1");
    assert(partition != NULL);

    const void *map_ptr;
    esp_partition_mmap_handle_t map_handle;

    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &map_ptr, &map_handle));
    ESP_LOGI(TAG, "Mapped partition to data memory address %p", map_ptr);
    // *(int *)map_ptr = 100;

    // char read_data[sizeof(store_data)];
    // memcpy(read_data, map_ptr, sizeof(read_data));
    // ESP_LOGI(TAG, "Read sample data from partition using mapped memory: %s", (char*) read_data);

    ESP_LOGI(TAG, "address region num in soc_cps.h: %u", SOC_MMU_LINEAR_ADDRESS_REGION_NUM);
    // static変数のため外部からのアクセスができない
    // ESP_LOGI(TAG, "address region num in mmu_map.h: %u", (unsigned int)s_mmu_ctx.num_regions);
    // for (int i = 0; i < s_mmu_ctx.num_regions; i++) {
    //     ESP_LOGI(TAG, "address region type in mmu_map.h: %u", (unsigned int)s_mmu_ctx.mem_regions[i].targets);    
    // }
    esp_mmu_map(0x10000, 0x1000, MMU_TARGET_PSRAM0, MMU_MEM_CAP_READ | MMU_MEM_CAP_8BIT, ESP_MMU_MMAP_FLAG_PADDR_SHARED, map_ptr);
    // *(int *)map_ptr = 100;
    // printf("%d\n", *(int *)map_ptr);

    esp_partition_munmap(map_handle);
}
