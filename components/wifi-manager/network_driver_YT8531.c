/* SPDX-License-Identifier: MIT
 * ESP32-S31-Function-CoreBoard-1 native Ethernet.
 * Pinout and PHY delays match the independently tested validation/FIP build.
 */
#include "sdkconfig.h"
#include "network_ethernet.h"

#if CONFIG_IDF_TARGET_ESP32S31
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy_yt8531.h"

static network_ethernet_driver_t driver;
static esp_netif_inherent_config_t netif_base;
static esp_netif_config_t netif_config;

static esp_err_t start(spi_device_handle_t unused, eth_config_t *board)
{
    (void)unused;
    /* The IDF S31 defaults use RXCLK14/TXCLK13, TXCTL12/TXD8..11,
     * RXCTL15/RXD19..16. Do not substitute the ESP32 RMII pinout. */
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = board->mdc;
    emac.smi_gpio.mdio_num = board->mdio;
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = -1;
    phy_config.reset_gpio_num = board->rst;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    if (!mac) return ESP_ERR_NO_MEM;
    esp_eth_phy_t *phy = esp_eth_phy_new_yt8531(&phy_config);
    if (!phy) {
        mac->del(mac);
        return ESP_ERR_NO_MEM;
    }
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&config, &driver.handle);
    if (err != ESP_OK) {
        phy->del(phy);
        mac->del(mac);
        return err;
    }
    yt8531_rgmii_clk_delay_config_t delays = {
        .rxc_dly_en = true, .rx_delay_sel = 0,
        .tx_delay_sel = 13, .tx_delay_sel_fe = 13,
    };
    err = esp_eth_ioctl(driver.handle,
        (esp_eth_io_cmd_t)YT8531_ETH_CMD_S_RGMII_CLK_DELAY, &delays);
    if (err != ESP_OK) {
        esp_eth_driver_uninstall(driver.handle);
        driver.handle = NULL;
        phy->del(phy);
        mac->del(mac);
    }
    return err;
}

static void init_config(eth_config_t *board)
{
    (void)board;
    netif_base = (esp_netif_inherent_config_t)ESP_NETIF_INHERENT_DEFAULT_ETH();
    netif_config.base = &netif_base;
    netif_config.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    driver.cfg_netif = &netif_config;
    driver.start = start;
}

network_ethernet_driver_t *YT8531_Detect(const char *name)
{
    if (!strcasestr(name, "YT8531")) return NULL;
    driver.valid = true;
    driver.rmii = false;
    driver.spi = false;
    driver.init_config = init_config;
    return &driver;
}
#endif
