| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |

# Hello World Example

Starts a FreeRTOS task to print "Hello World".

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

Follow detailed instructions provided specifically for this example.

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## Example folder contents

The project **hello_world** contains one source file in C language [hello_world_main.c](main/hello_world_main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt` files that provide set of directives and instructions describing the project's source files and targets (executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── pytest_hello_world.py      Python script used for automated testing
├── main
│   ├── CMakeLists.txt
│   └── hello_world_main.c
└── README.md                  This is the file you are currently reading
```

For more information on structure and contents of ESP-IDF projects, please refer to Section [Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html) of the ESP-IDF Programming Guide.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.

## External flashのMMUへのマッピングの流れ（esp_partition_mmap()を使用した場合）

1. esp_partition_mmap(ハードウェア上のアドレス、マップする領域のサイズ、メモリの種類（capabilitiesの設定）、仮想アドレス、out_handler(?))

    * components/esp_partition/partition_target
    * 主にアラインメントをして、次にspi_flash_mmap()を呼び出す
    * [ハンドルの説明](https://en.wikipedia.org/wiki/Handle_(computing))

2. spi_flash_mmap(ソースアドレス、サイズ、メモリの種類、仮想アドレス、ハンドラ)

    * components/spi_flash/flash_mmap
    * 明示的にexternal flashを選択し、esp_mmu_mmap()を呼び出す
    * heap_calloc()で割り当て、ハンドラを作成する

3. esp_mmu_mmap(ソースアドレス、サイズ、ターゲットデバイス(flash or psram)、capabilities、フラグ、仮想アドレス)

    * components/esp_mm/esp_mmu_map
    * フラグ：ex) あるソースアドレスが複数の仮想アドレスにマッピングされることを許可
    * ESP32はpsramをサポートしていない？
    * ここでどのハードのmmuにマッピングするかを指示する？

```c
#if !SOC_SPIRAM_SUPPORTED || CONFIG_IDF_TARGET_ESP32
    ESP_RETURN_ON_FALSE(!(target & MMU_TARGET_PSRAM0), ESP_ERR_NOT_SUPPORTED, TAG, "PSRAM is not supported");
#endif

    typedef enum {
        MMU_TARGET_FLASH0 = BIT(0),
        MMU_TARGET_PSRAM0 = BIT(1),
    } mmu_target_t;
```

## esp_mmu_map()の解析

### esp_mmu_map()の説明

```
esp_err_t esp_mmu_map(esp_paddr_t paddr_start, size_t size, mmu_target_t target, mmu_mem_caps_t caps, int flags, void **out_ptr)
Map a physical memory block to external virtual address block, with given capabilities.

パラメーター:
paddr_start – Start address of the physical memory block
size – Size to be mapped. Size will be rounded up by to the nearest multiple of MMU page size
target – Physical memory target you're going to map to, see mmu_target_t
caps – Memory capabilities, see mmu_mem_caps_t
flags – Mmap flags
out_ptr – Start address of the mapped virtual memory

戻り値:

ESP_OK - ESP_ERR_INVALID_ARG: Invalid argument, see printed logs - ESP_ERR_NOT_SUPPORTED: Only on ESP32, PSRAM is not a supported physical memory target - ESP_ERR_NOT_FOUND: No enough size free block to use - ESP_ERR_NO_MEM: Out of memory, this API will allocate some heap memory for internal usage - ESP_ERR_INVALID_STATE: Paddr is mapped already, this API will return corresponding vaddr_start of the previously mapped block. Only to-be-mapped paddr block is totally enclosed by a previously mapped block will lead to this error. (Identical scenario will behave similarly) new_block_start new_block_end |-------- New Block --------| |--------------- Block ---------------| block_start block_end
注:
This API does not guarantee thread safety
```

この関数は特定の物理メモリブロックをある仮想アドレスブロックに指示されたcapabilitiesに基づいてマップするものである。どの仮想アドレスブロックにマップするかは指定できない。本研究では仮想アドレスブロックを管理するプロセスを作成して、空いている特定の仮想アドレスブロックと物理メモリブロックのマッピングを行える関数の作成を第一の目標とする。そのためにはesp_mmu_map()がどのような流れで処理されているのかを理解する必要がある。

注）このAPIは[スレッド安全](https://ja.wikipedia.org/wiki/%E3%82%B9%E3%83%AC%E3%83%83%E3%83%89%E3%82%BB%E3%83%BC%E3%83%95)を保証しない

#### 想定されるエラー
* ESP_ERR_INVALID_ARG: 引数に関するエラー
* ESP_ERR_NOTE_SUPPORTED: __動いているMCUがESP32で、なおかつPSRAMをターゲットデバイスとしている場合に発生するエラー__
* ESP_ERR_NOT_FOUND: 十分なサイズのフリーブロックが存在しないとき発生するエラー（仮想？物理？）
* ESP_ERR_NO_MEM: メモリ不足の際に発生するエラー（ESP_ERR_NOT_FOUNDとの違いは？）
* ESP_ERR_INVALID_STATE: 物理アドレスが既にマップされている場合に発生するエラー、現在のマップ先の仮想アドレスを返す、[物理アドレブロックが完全に別の物理アドレスブロックに包含されている場合にのみ発生する](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mm.html#relation-between-memory-blocks)

#### 処理の流れ
```c
    esp_err_t ret = ESP_FAIL;
    ESP_RETURN_ON_FALSE(out_ptr, ESP_ERR_INVALID_ARG, TAG, "null pointer");
#if !SOC_SPIRAM_SUPPORTED || CONFIG_IDF_TARGET_ESP32
    ESP_RETURN_ON_FALSE(!(target & MMU_TARGET_PSRAM0), ESP_ERR_NOT_SUPPORTED, TAG, "PSRAM is not supported");
#endif
    ESP_RETURN_ON_FALSE((paddr_start % CONFIG_MMU_PAGE_SIZE == 0), ESP_ERR_INVALID_ARG, TAG, "paddr must be rounded up to the nearest multiple of CONFIG_MMU_PAGE_SIZE");
    ESP_RETURN_ON_ERROR(s_mem_caps_check(caps), TAG, "invalid caps");

```

最初は状態のチェックから始めている。