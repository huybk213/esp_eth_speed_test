/* Ethernet Basic Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define SPEED_TEST_WIFI 0
#define SPEED_TEST_ETH  1

#if CONFIG_ETH_USE_SPI_ETHERNET
#include "driver/spi_master.h"
#endif // CONFIG_ETH_USE_SPI_ETHERNET

static const char *TAG = "eth_example";

#define GL_INTERNET_CONNECTED_BIT BIT0
#define GL_INTERNET_DISCONNECTED_BIT BIT1

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

EventGroupHandle_t gl_wifi_event_group;

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_SERVER":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";
static void http_download_chunk(void);
static uint32_t prev_tick, avg_speed;
void wifi_init_sta(void);

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            prev_tick = xTaskGetTickCount();
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // // If user_data buffer is configured, copy the response into the buffer
                // if (evt->user_data) {
                //     memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                // } else {
                //     if (output_buffer == NULL) {
                //         output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                //         output_len = 0;
                //         if (output_buffer == NULL) {
                //             ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                //             return ESP_FAIL;
                //         }
                //     }
                //     memcpy(output_buffer + output_len, evt->data, evt->data_len);
                // }
                output_len += evt->data_len;
            }
            output_len += evt->data_len;

            break;
        case HTTP_EVENT_ON_FINISH:
        {
            uint32_t diff = xTaskGetTickCount() - prev_tick;
            static uint32_t download_cnt = 1;
            uint32_t tmp_speed = output_len * 1000 / (diff*1024);
            ESP_LOGI(TAG, "[%d]HTTP_EVENT_ON_FINISH %dKB on %dms, speed %dKB/s", download_cnt, output_len/1024, diff, tmp_speed);
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            avg_speed += tmp_speed;
            output_len = 0;
            prev_tick = 0;
            download_cnt++;
        }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            // int mbedtls_err = 0;
            // esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            // if (err != 0) {
            //     if (output_buffer != NULL) {
            //         free(output_buffer);
            //         output_buffer = NULL;
            //     }
            //     output_len = 0;
            //     ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            //     ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            // }
            break;
    }
    return ESP_OK;
}

static void http_download_chunk(void)
{
    esp_http_client_config_t config = {
        .url = "http://192.168.1.7/test.txt",
        .event_handler = _http_event_handler,
        .buffer_size = 4096 * 6,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "HTTP chunk encoding Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}
static void http_get_task(void *pvParameters)
{
    // const struct addrinfo hints = {
    //     .ai_family = AF_INET,
    //     .ai_socktype = SOCK_STREAM,
    // };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    // char recv_buf[4096];
    vTaskDelay(5000);
    while(1) {
        // int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        // if(err != 0 || res == NULL) {
        //     ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        //     continue;
        // }

        // /* Code to print the resolved IP.

        //    Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        // addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        // ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        // s = socket(res->ai_family, res->ai_socktype, 0);
        // if(s < 0) {
        //     ESP_LOGE(TAG, "... Failed to allocate socket.");
        //     freeaddrinfo(res);
        //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... allocated socket");

        // if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        //     ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        //     close(s);
        //     freeaddrinfo(res);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }

        // ESP_LOGI(TAG, "... connected");
        // freeaddrinfo(res);
        
        // if (write(s, REQUEST, strlen(REQUEST)) < 0) {
        //     ESP_LOGE(TAG, "... socket send failed");
        //     close(s);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... socket send success");


        // struct timeval receiving_timeout;
        // receiving_timeout.tv_sec = 5;
        // receiving_timeout.tv_usec = 0;
        // if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
        //         sizeof(receiving_timeout)) < 0) {
        //     ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        //     close(s);
        //     vTaskDelay(4000 / portTICK_PERIOD_MS);
        //     continue;
        // }
        // ESP_LOGI(TAG, "... set socket receiving timeout success");

        // uint32_t size = 0;
        // uint32_t prev_tick = xTaskGetTickCount();
        // /* Read HTTP response */
        // do {
        //     bzero(recv_buf, sizeof(recv_buf));
        //     r = read(s, recv_buf, sizeof(recv_buf)-1);
        //     // for(int i = 0; i < r; i++) {
        //     //     putchar(recv_buf[i]);
        //     // }
        //     size += r;
        // } while(r > 0);
        // uint32_t diff = xTaskGetTickCount() - prev_tick;
        // ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d, size %d, speed %dbytes/s, time %dms", r, errno,
        //                 size, 
        //                 size * 1000 /diff, diff);
        // close(s);
        // // for(int countdown = 10; countdown >= 0; countdown--) {
        // //     ESP_LOGI(TAG, "%d... ", countdown);
        // //     vTaskDelay(1000 / portTICK_PERIOD_MS);
        // // }

        // avg_speed = 0;
        // for (uint8_t i = 0; i < 30; i++)
        // {
        //     http_download_chunk();
        //     vTaskDelay(1);
        // }
        // ESP_LOGI(TAG, "ETH, avg speed %dKB/s\r\n", avg_speed/(30));
#if SPEED_TEST_WIFI
        wifi_init_sta();
#endif
        avg_speed = 0;
        for (uint8_t i = 0; i < 30; i++)
        {
            http_download_chunk();
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "avg speed %dKB/s\r\n", avg_speed/(30));

        while(1)
        {
            vTaskDelay(1000);
        }
    }
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

bool got_ip = false;
/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    got_ip = true;
}
uint32_t s_retry_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 6) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(gl_wifi_event_group, GL_INTERNET_DISCONNECTED_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(gl_wifi_event_group, GL_INTERNET_CONNECTED_BIT);
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }

}

void wifi_init_sta(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    gl_wifi_event_group = xEventGroupCreate();


    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "WIFI error %d %x\r\n", err, err);
    ESP_ERROR_CHECK(err);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "BYTECH_T1",
            .password = "bytech@2020",
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (GL_INTERNET_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (GL_INTERNET_DISCONNECTED_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(gl_wifi_event_group,
            GL_INTERNET_CONNECTED_BIT | GL_INTERNET_DISCONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & GL_INTERNET_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to AP");
    } else if (bits & GL_INTERNET_DISCONNECTED_BIT) {
        ESP_LOGI(TAG, "Failed to connect to AP");
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    // vEventGroupDelete(gl_wifi_event_group);
}


void app_main(void)
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if SPEED_TEST_ETH
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;
#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    mac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_EXAMPLE_ETH_PHY_IP101
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN8720
    esp_eth_phy_t *phy = esp_eth_phy_new_lan8720(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_KSZ8041
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz8041(&phy_config);
#endif
#elif CONFIG_ETH_USE_SPI_ETHERNET
    ESP_LOGI(TAG, "\r\nSLCK %d\r\nMOSI %d\r\nMISO %d\r\nCS %d\r\nINT %d\r\nRST %d\r\nClock %dMhz\r\n",
                CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
                CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
                CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
                CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
                CONFIG_EXAMPLE_ETH_SPI_INT_GPIO,
                CONFIG_EXAMPLE_ETH_PHY_RST_GPIO,
                CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ
                );
    gpio_install_isr_service(0);
    spi_device_handle_t spi_handle = NULL;
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, 1));
#if CONFIG_EXAMPLE_USE_DM9051
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* dm9051 ethernet driver is based on spi driver */
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle);
    dm9051_config.int_gpio_num = CONFIG_EXAMPLE_ETH_SPI_INT_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_EXAMPLE_USE_W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* w5500 ethernet driver is based on spi driver */
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = CONFIG_EXAMPLE_ETH_SPI_INT_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
#endif
#endif // CONFIG_ETH_USE_SPI_ETHERNET
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
#if CONFIG_ETH_USE_SPI_ETHERNET
    /* The SPI Ethernet module might doesn't have a burned factory MAC address, we cat to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56
    }));
#endif
    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    /* start Ethernet driver state machine */
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    while (got_ip == false)
    {
        vTaskDelay(1000);
    }
#endif /* SPEED_TEST_ETH */

    xTaskCreate(&http_get_task, "http_get_task", 8192, NULL, 5, NULL);
}
