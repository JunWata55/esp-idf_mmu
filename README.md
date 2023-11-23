# MMUをesp32上で自由に使えるようにしよう
[external-flashのmmuへのマッピングの流れ](#external-flashのmmuへのマッピングの流れesp_partition_mmapを使用した場合)<br>
[esp_mmu_mapの解析](#esp_mmu_mapの解析)<br>
[esp_mmu_mapの説明](#esp_mmu_mapの説明)<br>

## はじめに
使用しているMCUはM5Stack Fireであり、乗っているチップはESPIDFによるとESP32-D0WDQ6-V3である。
16MBのexternal memoryを積んでいるが、全部を利用可能にするためにはmenuconfigからserial flasher config->flash sizeで設定を変更する必要がある？

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

### 処理の流れ
#### 実行前処理
```c
    esp_err_t ret = ESP_FAIL;
    ESP_RETURN_ON_FALSE(out_ptr, ESP_ERR_INVALID_ARG, TAG, "null pointer");
#if !SOC_SPIRAM_SUPPORTED || CONFIG_IDF_TARGET_ESP32
    ESP_RETURN_ON_FALSE(!(target & MMU_TARGET_PSRAM0), ESP_ERR_NOT_SUPPORTED, TAG, "PSRAM is not supported");
#endif
    ESP_RETURN_ON_FALSE((paddr_start % CONFIG_MMU_PAGE_SIZE == 0), ESP_ERR_INVALID_ARG, TAG, "paddr must be rounded up to the nearest multiple of CONFIG_MMU_PAGE_SIZE");
    ESP_RETURN_ON_ERROR(s_mem_caps_check(caps), TAG, "invalid caps");

```

まずは実行前状態のチェックから入る。最初は戻り値を初期化している。次にout_ptrの参照先が存在しない（＝out_ptrがNULL）なら、引数エラーを返す。そして、SPIRAMを対応していないデバイス上でSPIRAMを利用としている場合はエラーを発生させる。次は物理アドレスがページのアラインメントに従っているかをチェックしている。そして最後に設定したcapabilityをチェックする。このs_mem_cpas_check()関数は以下の通りになっている。

```c
static esp_err_t s_mem_caps_check(mmu_mem_caps_t caps)
{
    if (caps & MMU_MEM_CAP_EXEC) {
        if ((caps & MMU_MEM_CAP_8BIT) || (caps & MMU_MEM_CAP_WRITE)) {
            //None of the executable memory are expected to be 8-bit accessible or writable.
            return ESP_ERR_INVALID_ARG;
        }
        caps |= MMU_MEM_CAP_32BIT;
    }
    return ESP_OK;
}
```

ここで重要になってくるのはMMU_MEM_CAP_EXECマクロである。このマクロは実行可能領域として物理アドレスをマッピングすることを表し、その際に8ビット読み込みや書き出し可能のcapabilityを同時に設定してはいけない。なぜなら実行可能領域は32bitでしか読み込めず、またプログラムを保護するために書き出すことはできない。設定されているcapabilityが健全であることをチェックしたら、OKを返す。

参考記事：
* [iramは32 bit alignedされる必要がある](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html#memory-capabilities)
* [32 bit capabilityを設定する際の留意点](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html#bit-accessible-memory)


#### 利用可能な仮想記憶ブロックの探索: _s_find_available_region()_
```c
    size_t aligned_size = ALIGN_UP_BY(size, CONFIG_MMU_PAGE_SIZE);
    int32_t found_region_id = s_find_available_region(s_mmu_ctx.mem_regions, s_mmu_ctx.num_regions, aligned_size, caps, target);
    if (found_region_id == -1) {
        ESP_EARLY_LOGE(TAG, "no such vaddr range");
        return ESP_ERR_NOT_FOUND;
    }
```

ALIGN_UP_BYは要求するメモリ領域のサイズsize以上のCONFIG_MMU_PAGE_SIZEの倍数の中で、最小のものを返すマクロである。これによりアラインメントの規則を守りながらメモリを最小限に抑えることができる。例えばCONFIG_MMU_PAGE_SIZEが0x100の場合、sizeが0x99なら0x100、0x100なら0x100、0x101なら0x200を返すようになっている。次にs_find_availble_region()を呼び出して、利用可能なブロックが残されていれば次の処理を続ける。ちなみにs_mmu_ctxのctxはコンテキストのことらしい。

```c
static int32_t s_find_available_region(mem_region_t *mem_regions, uint32_t region_nums, size_t size, mmu_mem_caps_t caps, mmu_target_t target)
{
    int32_t found_region_id = -1;
    for (int i = 0; i < region_nums; i++) {
        if (((mem_regions[i].caps & caps) == caps) && ((mem_regions[i].targets & target) == target)) {
            if (mem_regions[i].max_slot_size >= size) {
                found_region_id = i;
                break;
            }
        }
    }
    return found_region_id;
}
```
* mem_regions: 仮装記憶領域（iramとかdromとかdramとか）、mem_regionsごとにtargetが決まっている
* regioin_nums: mem_regionsの数
* size: ページサイズにアラインメントされたマッピングに必要な領域の大きさ
* caps: [チェックされた後のcapability](#実行前処理)
* target: 物理メモリを提供するターゲットデバイス

フリーな仮想記憶領域の中で、要求しているcapabilityとtargetを満たすものの中で、size以上のスロットを持っている場合は、その領域のidを返す。-1が返されたら該当の領域は見つからなかったことになる。これを呼び出しているs_mmu_ctxはフリーな仮想記憶領域の情報を保持している。
__＊ESP32では仮装アドレスはアドレス領域ごとに異なるデバイスにマップされるため、仮想記憶領域ごとにtargetとcapabilityが設定されている。__
以下は各仮想記憶領域を管理するmmu_ctx_tという構造体の定義と、かく仮想記憶領域で保持される情報をまとめたmem_region_という構造体の定義である。以下のフィールドの中で留意すべきものを示す。
* free_head: 領域内で未使用部分の開始アドレス
__（多分esp32では解放されてもコンパクションが行われるまでは再利用されない？）__
* mem_block_head: 割り当てられたブロックの情報を格納しているリスト
__（解放時にこのリストに対して操作？コンパクションをやるとしたらこれを基にやる？）__

__ちなみにTAILQは[tail queue](https://www.hazymoon.jp/OpenBSD/sys/queue/tail_queue.html)のことらしい。__

```c
typedef struct {
    /**
     * number of memory regions that are available, after coalescing, this number should be smaller than or equal to `SOC_MMU_LINEAR_ADDRESS_REGION_NUM`
     */
    uint32_t num_regions;
    /**
     * This saves the available MMU linear address regions,
     * after reserving flash .rodata and .text, and after coalescing.
     * Only the first `num_regions` items are valid
     */
    mem_region_t mem_regions[SOC_MMU_LINEAR_ADDRESS_REGION_NUM];
} mmu_ctx_t;
```

```c
typedef struct mem_region_ {
    cache_bus_mask_t bus_id;  //cache bus mask of this region
    uint32_t start;           //linear address start of this region
    uint32_t end;             //linear address end of this region
    size_t region_size;       //region size, in bytes
    uint32_t free_head;       //linear address free head of this region
    size_t max_slot_size;     //max slot size within this region
    int caps;                 //caps of this region, `mmu_mem_caps_t`
    mmu_target_t targets;     //physical targets that this region is supported
    TAILQ_HEAD(mem_block_head_, mem_block_) mem_block_head;      //link head of allocated blocks within this region
} mem_region_t;
```

#### 探索されたブロックに対する操作

```c
    mem_region_t *found_region = &s_mmu_ctx.mem_regions[found_region_id];
    mem_block_t *dummy_head = NULL;
    mem_block_t *dummy_tail = NULL;
    mem_block_t *new_block = NULL;
```
まずは必要な変数を初期化する。

次に実際に〜〜〜していく。最初に見ているTAILQ_EMPTYはmem_block_headが指すリストの先頭要素(tqh_first)がNULLかどうかを見ている。つまり〜〜〜のとき、、、
internal heap上にdummy_head(以降dhと呼ぶ)を作成し、dhのladdr_startとladdr_endを共にfound_regionのfree_headにしている。
TAILQ_INSERT_HEADはdummy_headをtail queueの先頭に挿入するマクロであり、TAILQ _INSERT _TAILは末尾に挿入するマクロである。
__こうすることで多分リストが空かどうかを意識することなくこれ以降の探索を行えるようになると思う。__

__これ重要！空の時この操作がされるということは、全てのtail queueは先頭と末尾にダミーセルを持っていることを示す__

```c
    if (TAILQ_EMPTY(&found_region->mem_block_head)) {
        dummy_head = (mem_block_t *)heap_caps_calloc(1, sizeof(mem_block_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_GOTO_ON_FALSE(dummy_head, ESP_ERR_NO_MEM, err, TAG, "no mem");

        dummy_head->laddr_start = found_region->free_head;
        dummy_head->laddr_end = found_region->free_head;
        //We don't care vaddr or paddr address for dummy head
        dummy_head->size = 0;
        dummy_head->caps = caps;
        TAILQ_INSERT_HEAD(&found_region->mem_block_head, dummy_head, entries);

        dummy_tail = (mem_block_t *)heap_caps_calloc(1, sizeof(mem_block_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_GOTO_ON_FALSE(dummy_tail, ESP_ERR_NO_MEM, err, TAG, "no mem");

        dummy_tail->laddr_start = found_region->end;
        dummy_tail->laddr_end = found_region->end;
        //We don't care vaddr or paddr address for dummy tail
        dummy_tail->size = 0;
        dummy_tail->caps = caps;
        TAILQ_INSERT_TAIL(&found_region->mem_block_head, dummy_tail, entries);
    }
```

以下がmem_block_(mem_block_t)の定義である。TAILQ_ENTRYはtail queueの要素を表す。mem_block_は物理アドレスと仮装アドレスのいずれかを表現することができる。
```c
typedef struct mem_block_ {
    uint32_t laddr_start;  //linear address start of this block
    uint32_t laddr_end;    //linear address end of this block
    intptr_t vaddr_start;  //virtual address start of this block
    intptr_t vaddr_end;    //virtual address end of this block
    size_t size;           //size of this block, should be aligned to MMU page size
    int caps;              //caps of this block, `mmu_mem_caps_t`
    uint32_t paddr_start;  //physical address start of this block
    uint32_t paddr_end;    //physical address end of this block
    mmu_target_t target;   //physical target that this block is mapped to
    TAILQ_ENTRY(mem_block_) entries;  //link entry
} mem_block_t;
```

ブロックはマップされたされた仮想アドレス領域に対応する、tail queueで保存される
スロットはブロック間のフリースペースを表す、そこを探索することでまだマップされていないアドレス領域を探索することができる

実際のマッピングをしている関数はs_do_mapping()である。


### s_do_mapping()の処理の流れ

```c
    // Suspend the scehduler on both CPUs, disable cache.
    // Contrary to its name this doesn't do anything with interrupts, yet.
    // Interrupt disabling capability will be added once we implement interrupt allocation API.
    spi_flash_disable_interrupts_caches_and_other_cpu();

    uint32_t actual_mapped_len = s_mapping_operation(target, vaddr_start, paddr_start, size);

    cache_bus_mask_t bus_mask = cache_ll_l1_get_bus(0, vaddr_start, size);
    cache_ll_l1_enable_bus(0, bus_mask);

    bus_mask = cache_ll_l1_get_bus(0, vaddr_start, size);
    cache_ll_l1_enable_bus(1, bus_mask);

    s_do_cache_invalidate(vaddr_start, size);

    // Enable cache, enable interrupts (to be added in future), resume scheduler
    spi_flash_enable_interrupts_caches_and_other_cpu();
```

spi_flash_disable_interrupts_caches_and_other_cpu()とspi_flash_enable_interrupts_caches_and_other_cpu()はそれぞれフラッシュに対する操作を行う際に他のプロセスによってインターリーブ等の問題が発生することを防ぐために割込みを一時的に禁止する領域を指定できる。
前者の関数はクリティカルセクションの開始地点を表し、後者はその終了地点を表す。
