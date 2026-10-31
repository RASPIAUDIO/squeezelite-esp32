#include <stdio.h>
#include <string.h>
#include "application_name.h"
#include "esp_err.h"
#include "esp_app_desc.h"

extern esp_err_t process_recovery_ota(const char * bin_url, char * bin_buffer, uint32_t length);

const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = PROJECT_VER,
    .project_name = CONFIG_PROJECT_NAME,
    .idf_ver = IDF_VER,
#if CONFIG_IDF_TARGET_ESP32S31
    // Match IDF 6.1's bootloader contract, including the MMU page size.
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
    .spi_flash_mode = CONFIG_ESPTOOLPY_FLASHMODE_VAL,
#endif

#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#else
    .secure_version = 0,
#endif

#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#else
    .time = "",
    .date = "",
#endif
};

void register_optional_cmd(void) {
}    

int main(int argc, char **argv){
	return 1;
}

void register_squeezelite(){
}

void register_external(void) {
}

void deregister_external(void) {
}

void decode_restore(int external) {
}

esp_err_t start_ota(const char * bin_url, char * bin_buffer, uint32_t length)
{
		return process_recovery_ota(bin_url,bin_buffer,length);
}
