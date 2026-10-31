/* Improv Serial v1, https://www.improv-wifi.com/serial/.
 * USB/UART only: no BLE, no changes to audio or Bluetooth configuration. */
#include "improv_serial.h"
#include "improv_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "network_manager.h"
#include "network_wifi.h"
#include "network_status.h"
#include "network_ethernet.h"
#include "platform_config.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern bool bypass_network_manager;
static improv_parser_t parser;
typedef struct {uint8_t data[255]; uint16_t len; bool valid;} request_t;
static QueueHandle_t requests;
static atomic_uint ip_generation;
static unsigned pending_generation;
static bool connecting;
static char pending_ssid[33];
static int64_t deadline;

static void erase(void *p,size_t n) {volatile uint8_t *v=p;while(n--) *v++=0;}
static uint32_t now_ms(void) {return (uint32_t)(esp_timer_get_time()/1000);}
static bool enabled(void) {
    char *v=config_alloc_get(NVS_TYPE_STR,"improv_serial");
    bool yes=!v || !strcasecmp(v,"Y") || !strcmp(v,"1"); free(v);return yes;
}
static void packet(uint8_t type,const uint8_t *data,size_t n) {
    if(n>255) return;
    /* The browser SDK recognizes IMPROV only at the start of a line.
     * linenoise leaves an unterminated prompt on UART, so terminate it before
     * every response. Both separators stay outside the protocol checksum. */
    uint8_t b[267]={'\n','I','M','P','R','O','V',1,type,(uint8_t)n};
    memcpy(b+10,data,n);uint8_t sum=0;
    for(size_t i=1;i<n+10;i++) sum+=b[i];
    b[n+10]=sum;b[n+11]='\n';
    /* One driver write holds the UART TX lock for the entire binary packet.
     * Do not use stdio: its CRLF translation corrupts checksum/data bytes. */
    uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM,b,n+12);
}
static void error(uint8_t e) {packet(2,&e,1);}
static void state(uint8_t s) {packet(1,&s,1);}
static void result(uint8_t cmd,const char **strings,size_t count) {
    uint8_t data[255]={cmd,0};size_t pos=2;
    for(size_t i=0;i<count;i++) {
        size_t n=strlen(strings[i]);
        if(n>255 || pos+1+n>sizeof(data)) {error(255);return;}
        data[pos++]=(uint8_t)n;memcpy(data+pos,strings[i],n);pos+=n;
    }
    data[1]=(uint8_t)(pos-2);packet(4,data,pos);
}
static bool interface_url(esp_netif_t *netif,char url[64]) {
    esp_netif_ip_info_t ip;
    if(!netif || !esp_netif_is_netif_up(netif) || esp_netif_get_ip_info(netif,&ip)!=ESP_OK || !ip.ip.addr) return false;
    snprintf(url,64,"http://" IPSTR "/",IP2STR(&ip.ip));return true;
}
static bool wifi_url(char url[64]) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap)==ESP_OK && interface_url(network_wifi_get_interface(),url);
}
static bool online_url(char url[64]) {
    return interface_url(network_ethernet_get_interface(),url) || wifi_url(url);
}
static void reply_state(void) {
    char url[64];
    if(!enabled() || bypass_network_manager) state(0);
    else if(connecting) state(3);
    else if(wifi_url(url)) {state(4);const char *s[]={url};result(2,s,1);}
    else state(2);
}
static void got_ip(void *arg,esp_event_base_t base,int32_t id,void *data) {
    atomic_fetch_add(&ip_generation,1);
}
static void scan_results(void) {
    /* Scan only when the setup client requests networks. The manager owns
     * driver results; wait for its JSON cache, never consume those results
     * here. Repeated browser polls reuse the cache for 30 seconds. */
    static int64_t last_scan;
    int64_t now = esp_timer_get_time();
    if (!last_scan || now - last_scan > 30000000) {
        unsigned generation = network_wifi_scan_generation();
        network_async_scan();
        int64_t until = now + 15000000;
        while (network_wifi_scan_generation() == generation && esp_timer_get_time() < until)
            vTaskDelay(pdMS_TO_TICKS(50));
        if (network_wifi_scan_generation() == generation) {error(255);return;}
        last_scan = esp_timer_get_time();
    }
    if(!network_status_lock_json_buffer(pdMS_TO_TICKS(500))) {error(255);return;}
    char *json=network_status_alloc_get_ap_list_json();
    network_status_unlock_json_buffer();
    cJSON *list=json ? cJSON_Parse(json) : NULL;free(json);
    cJSON *ap;
    cJSON_ArrayForEach(ap,list) {
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(ap,"ssid");
        cJSON *rssi=cJSON_GetObjectItemCaseSensitive(ap,"rssi");
        cJSON *auth=cJSON_GetObjectItemCaseSensitive(ap,"auth");
        if(!cJSON_IsString(ssid) || !ssid->valuestring[0] || !cJSON_IsNumber(rssi) || !cJSON_IsNumber(auth)) continue;
        char strength[16];snprintf(strength,sizeof(strength),"%d",rssi->valueint);
        const char *s[]={ssid->valuestring,strength,auth->valueint==WIFI_AUTH_OPEN ? "NO":"YES"};result(4,s,3);
    }
    cJSON_Delete(list);result(4,NULL,0);
}
static void handle(request_t *r) {
    if(!r->valid || r->len<2 || r->data[1]!=r->len-2) {error(1);return;}
    uint8_t cmd=r->data[0];size_t n=r->data[1];const uint8_t *p=r->data+2;
    error(0);
    if(cmd==2 && !n) {reply_state();return;}
    if(cmd==3 && !n) {
        char *name=config_alloc_get(NVS_TYPE_STR,"hostname");
        const char *s[]={"MnCast Squeezelite",esp_app_get_description()->version,CONFIG_IDF_TARGET,name ? name:"MnCast"};
        result(3,s,4);free(name);return;
    }
    if(cmd==7 && !n) {
        char url[64];bool online=online_url(url);
        const char *s[]={online ? "7":"6",url};result(7,s,online ? 2:1);return;
    }
    if(!enabled() || bypass_network_manager) {state(0);error(255);return;}
    if(cmd==4 && !n) {scan_results();return;}
    if(cmd!=1) {error(2);return;}
    if(connecting) {error(255);return;}
    if(n<2 || !p[0] || p[0]>32 || (size_t)p[0]+2>n) {error(1);return;}
    size_t sl=p[0],pl=p[1+sl];
    if(pl>64 || sl+pl+2!=n || memchr(p+1,0,sl) || memchr(p+2+sl,0,pl)) {error(1);return;}
    char online[64];
    if(!is_wifi_up() && !interface_url(network_ethernet_get_interface(),online)) {state(0);error(255);return;}
    char password[65]={0};
    memcpy(pending_ssid,p+1,sl);pending_ssid[sl]=0;memcpy(password,p+2+sl,pl);
    pending_generation=atomic_load(&ip_generation);
    connecting=true;deadline=esp_timer_get_time()+60000000;
    state(3);
    network_async_connect(pending_ssid,password);
    erase(password,sizeof(password));
}
static void worker(void *arg) {
    request_t r;
    for(;;) {
        if(xQueueReceive(requests,&r,connecting ? pdMS_TO_TICKS(250):portMAX_DELAY)==pdTRUE) {
            handle(&r);erase(&r,sizeof(r));
        }
        if(!connecting) continue;
        char url[64];wifi_ap_record_t ap;
        if(atomic_load(&ip_generation)!=pending_generation && wifi_url(url) &&
           esp_wifi_sta_get_ap_info(&ap)==ESP_OK && !strncmp((char*)ap.ssid,pending_ssid,32)) {
            connecting=false;state(4);const char *s[]={url};result(1,s,1);erase(pending_ssid,sizeof(pending_ssid));
        } else if(esp_timer_get_time()>deadline) {
            connecting=false;error(3);state(2);erase(pending_ssid,sizeof(pending_ssid));
        }
    }
}
static void received(const uint8_t *data,size_t len,bool valid) {
    if(!requests) return;
    request_t r={.len=(uint16_t)len,.valid=valid};memcpy(r.data,data,len);
    /* Never block console input behind network operations. */
    if(xQueueSend(requests,&r,0)!=pdTRUE) error(255);
    erase(&r,sizeof(r));
}
void improv_serial_init(void) {
    if(requests || bypass_network_manager) return;
    requests=xQueueCreate(2,sizeof(request_t));
    if(!requests) return;
    if(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,got_ip,NULL)!=ESP_OK) {
        vQueueDelete(requests);requests=NULL;return;
    }
    if(xTaskCreate(worker,"improv",4096,NULL,1,NULL)!=pdPASS) {
        esp_event_handler_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,got_ip);
        vQueueDelete(requests);requests=NULL;
    }
}
void improv_serial_feed(uint8_t b) {improv_parser_feed(&parser,b,now_ms(),received);}
size_t improv_serial_read_console(void *out,size_t cap) {
    improv_parser_expire(&parser,now_ms());return improv_parser_read(&parser,out,cap);
}
unsigned improv_serial_wait_ms(void) {return parser.used ? 100:0;}
