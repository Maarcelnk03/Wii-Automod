#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "test-config.h"
static bool started,sd_ok;
static const char *testfile="/sdcard/WIFI4M.BIN";
void bench_prepare_sd(void) {
 sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.flags=SDMMC_HOST_FLAG_1BIT;
 sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
 esp_vfs_fat_mount_config_t mc={.format_if_mount_failed=false,.max_files=4,.allocation_unit_size=16384};sdmmc_card_t *card=NULL;
 host.max_freq_khz=SDMMC_FREQ_HIGHSPEED;esp_err_t e=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&mc,&card);
 if(e!=ESP_OK){printf("BENCH SD 40 MHz fehlgeschlagen (%s), versuche 20 MHz\n",esp_err_to_name(e));host.max_freq_khz=SDMMC_FREQ_DEFAULT;card=NULL;e=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&mc,&card);}
 if(e==ESP_OK)printf("BENCH SD-Takt: %d kHz\n",card->real_freq_khz);
 if(e!=ESP_OK){printf("BENCH ESP-SD nicht bereit: %s\n",esp_err_to_name(e));return;}
 uint8_t *buf=malloc(16384);if(!buf)return;for(unsigned i=0;i<16384;i++)buf[i]=i;
 struct stat st;if(stat(testfile,&st)<0){FILE *f=fopen(testfile,"wb");if(f){unsigned done=0;while(done<TEST_BYTES&&fwrite(buf,1,16384,f)==16384)done+=16384;fclose(f);}}
 FILE *f=fopen(testfile,"rb");if(!f){printf("BENCH ESP-SD Testdatei nicht lesbar: %s\n",strerror(errno));free(buf);return;}
 unsigned count=0;uint32_t crc=0;int64_t start=esp_timer_get_time();size_t n;
 while((n=fread(buf,1,16384,f))>0){crc=esp_rom_crc32_le(crc,buf,n);count+=n;}
 int64_t us=esp_timer_get_time()-start;fclose(f);free(buf);
 sd_ok=count==TEST_BYTES&&crc==TEST_CRC;
 printf("BENCH ESP-SD: %u Byte in %lld us, %.3f MiB/s, CRC %s\n",count,(long long)us,(count/1048576.0)/(us/1000000.0),sd_ok?"OK":"FEHLER");
}
static int send_all(int sock,const void *data,size_t n) {
 const char *p=data;while(n){int w=send(sock,p,n,0);if(w<=0)return -1;p+=w;n-=w;}return 0;
}

#define NB 3
#define CH 16384
typedef struct{int idx;int n;}chunk_t;
static uint8_t *pool[NB];static QueueHandle_t q_full,q_free;
typedef struct{FILE *f;volatile int stop;volatile int finished;}reader_arg_t;
static void reader_task(void *arg){
 reader_arg_t *ra=(reader_arg_t*)arg;FILE *f=ra->f;
 for(;;){int idx;if(xQueueReceive(q_free,&idx,portMAX_DELAY)!=pdTRUE)break;if(ra->stop)break;
  chunk_t c={idx,(int)read(fileno(f),pool[idx],CH)};xQueueSend(q_full,&c,portMAX_DELAY);if(c.n!=CH)break;}
 ra->finished=1;
 vTaskDelete(NULL);
}
static void server_task(void *arg) {
 vTaskDelay(pdMS_TO_TICKS(500));
 printf("BENCH Heap vor BT-Freigabe: frei %u, groesster Block %u\n",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 {esp_err_t a=esp_bt_controller_disable();esp_err_t b=esp_bt_controller_deinit();esp_err_t m=esp_bt_mem_release(ESP_BT_MODE_BTDM);printf("BENCH BT: disable %d, deinit %d, mem_release %d\n",(int)a,(int)b,(int)m);}
 printf("BENCH Heap nach BT-Freigabe: frei %u, groesster Block %u\n",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 esp_err_t r=nvs_flash_init();if(r==ESP_ERR_NVS_NO_FREE_PAGES||r==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();nvs_flash_init();}
 esp_netif_init();esp_event_loop_create_default();esp_netif_create_default_wifi_ap();
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
 wifi_config_t config={0};strcpy((char*)config.ap.ssid,TEST_SSID);config.ap.ssid_len=strlen(TEST_SSID);strcpy((char*)config.ap.password,TEST_PASS);config.ap.authmode=WIFI_AUTH_WPA2_PSK;config.ap.channel=1;config.ap.max_connection=2;config.ap.pmf_cfg.required=false;
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&config));ESP_ERROR_CHECK(esp_wifi_start());esp_wifi_set_ps(WIFI_PS_NONE);esp_wifi_set_max_tx_power(78);
 int srv=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);int reuse=1;setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(8080),.sin_addr.s_addr=htonl(INADDR_ANY)};
 if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))<0||listen(srv,2)<0){puts("BENCH TCP-Server fehlgeschlagen");vTaskDelete(NULL);return;}
 printf("BENCH Heap bei AP-Start: frei %u, groesster Block %u\n",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 printf("BENCH AP BEREIT: %s, 192.168.4.1:8080\n",TEST_SSID);
 uint8_t *buf=malloc(16384);if(!buf){vTaskDelete(NULL);return;}
 for(int i=0;i<NB;i++){pool[i]=malloc(CH);if(!pool[i]){puts("BENCH Pool zu klein");vTaskDelete(NULL);return;}}
 q_full=xQueueCreate(NB+1,sizeof(chunk_t));q_free=xQueueCreate(NB+1,sizeof(int));
 for(;;){
  int client=accept(srv,NULL,NULL);if(client<0)continue;
  struct timeval tv={.tv_sec=15};setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  char req[256]={0};int n=0;
  while(n<(int)sizeof(req)-1&&!strstr(req,"\r\n\r\n")){
   int got=recv(client,req+n,sizeof(req)-1-n,0);if(got<=0)break;n+=got;req[n]=0;
  }
  bool from_sd=n>0&&!strncmp(req,"GET /sd ",8);bool from_ram=n>0&&!strncmp(req,"GET /ram ",9);
  unsigned total=TEST_BYTES;char fname[48]="";FILE *f=NULL;int is_file=0;
  if(!from_sd&&!from_ram&&n>5&&!strncmp(req,"GET /",5)){
   // Dateiabruf aus dem Wurzelverzeichnis der ESP-SD: nur einfache Namen
   const char *q=req+5;int l=0;while(q[l]&&q[l]!=' '&&q[l]!='\r'&&l<(int)sizeof(fname)-1){char ch=q[l];
    if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='.'||ch=='_'||ch=='-')){l=-1;break;}fname[l]=ch;l++;}
   if(l>0&&fname[0]!='.'&&(q[l]==' '||q[l]=='\r')){
    fname[l]=0;char path[64];snprintf(path,sizeof(path),"/sdcard/%s",fname);struct stat st;
    if(sd_ok&&stat(path,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>0){f=fopen(path,"rb");total=(unsigned)st.st_size;is_file=1;}
   }
  } else if(from_sd&&sd_ok)f=fopen(testfile,"rb");
  if(!from_ram&&!f){const char fail[]="HTTP/1.0 404 nicht gefunden\r\nContent-Length: 0\r\n\r\n";send_all(client,fail,sizeof(fail)-1);close(client);continue;}
  char header[160];snprintf(header,sizeof(header),"HTTP/1.0 200 OK\r\nContent-Length: %u\r\nX-CRC32: %08x\r\nConnection: close\r\n\r\n",total,is_file?0u:(unsigned)TEST_CRC);
  if(send_all(client,header,strlen(header))<0){if(f)fclose(f);close(client);continue;}
  unsigned sent=0;int64_t start=esp_timer_get_time();
  if(f){
   reader_arg_t ra={f,0,0};while(xQueueReceive(q_full,&(chunk_t){0},0)==pdTRUE);while(xQueueReceive(q_free,&(int){0},0)==pdTRUE);
   for(int i=0;i<NB;i++)xQueueSend(q_free,&i,0);
   xTaskCreatePinnedToCore(reader_task,"sdread",4096,&ra,6,NULL,1);
   while(sent<total){
    chunk_t c;
    if(xQueueReceive(q_full,&c,pdMS_TO_TICKS(5000))!=pdTRUE){break;}
    if(c.n<=0){break;}
    int ok=(send_all(client,pool[c.idx],c.n)==0);
    xQueueSend(q_free,&c.idx,0);
    if(!ok){break;}
    sent+=c.n;
   }
   ra.stop=1;
   int wake=0;
   xQueueSend(q_free,&wake,0);
   for(int w=0;w<50&&!ra.finished;w++)vTaskDelay(1);
   fclose(f);
  } else {
   for(unsigned i=0;i<CH;i++)buf[i]=i;
   while(sent<total){if(send_all(client,buf,CH)<0)break;sent+=CH;}
  }
  close(client);printf("BENCH %s: %u Byte, %lld us\n",from_ram?"RAM":(is_file?fname:"SD"),sent,(long long)(esp_timer_get_time()-start));
 }
}
void bench_start(void){if(started)return;started=true;xTaskCreate(server_task,"wlanbench",8192,NULL,5,NULL);}
