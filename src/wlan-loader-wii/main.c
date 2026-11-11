/* main.c - Teil von "Wii Automod (WLAN-Lader)"
 * Copyright (C) 2026 Maarcelnk03
 *
 * Dieses Programm ist freie Software: Sie koennen es unter den Bedingungen der
 * GNU General Public License, Version 2 oder (nach Ihrer Wahl) jeder spaeteren
 * Version, weitergeben und/oder veraendern. Siehe LICENSES/GPL-2.0.txt.
 *
 * Die Veroeffentlichung erfolgt in der Hoffnung, dass es nuetzlich ist, aber
 * OHNE JEDE GEWAEHRLEISTUNG. Siehe die GNU General Public License fuer Details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include "sdfmt.h"
#include "fortschritt.h"
#include "font.h"
#include "test-config.h"
static PrintConsole console;
static u8 orig[7004] ATTRIBUTE_ALIGN(32), cfg[7004] ATTRIBUTE_ALIGN(32), chk[7004] ATTRIBUTE_ALIGN(32);
static const char CFGPATH[]="/shared2/sys/net/02/config.dat" ;
static s32 cfgfd=-1;static int modified=0,have_orig=0;
int __IOS_LoadStartupIOS(void) {return 0;}
static u32 crc32(const u8 *p,unsigned n);
static int cfg_write(const u8 *data) {
 if(ISFS_Seek(cfgfd,0,0)<0)return -1;
 s32 w=ISFS_Write(cfgfd,data,7004);return w==7004?0:(w<0?w:-2);
}
static int cfg_read(u8 *data) {
 memset(data,0,7004);DCFlushRange(data,7004);
 if(ISFS_Seek(cfgfd,0,0)<0)return -1;
 s32 r=ISFS_Read(cfgfd,data,7004);DCInvalidateRange(data,7004);return r==7004?0:(r<0?r:-2);
}
static void restore_config(void) {
 if(!modified||!have_orig)return;
 if(cfgfd<0){int ri=ISFS_Initialize();cfgfd=ISFS_Open(CFGPATH,ISFS_OPEN_RW);printf("ISFS neu: init %d, open %d\n",ri,cfgfd);if(cfgfd<0)return;}
 int w=cfg_write(orig);int r=w==0?cfg_read(chk):-1;
 int ok=w==0&&r==0&&!memcmp(chk,orig,7004);
 printf("Original-WLAN-Konfiguration zurueckgeschrieben und geprueft: %s (w %d, r %d)\n",ok?"OK":"FEHLER",w,r);
 if(ok)modified=0;
}
/* ---- Protokoll: Bildschirmtext mitschreiben und als Datei auf die Wii-SD legen (sd:/wii/protokoll/Lader-<Datum>_<Zeit>.txt) ---- */
#include <sys/iosupport.h>
#include <time.h>
#define PROT_CAP 65536
static char prot_buf[PROT_CAP];static unsigned prot_len,prot_written,prot_line_start;static char prot_path[96];
static devoptab_t prot_dev;static const devoptab_t *prot_orig;
static ssize_t prot_write(struct _reent *r,void *fd,const char *ptr,size_t len){
 for(size_t i=0;i<len;i++){char c=ptr[i];
  if(c=='\r'){if(i+1<len&&ptr[i+1]=='\n')continue;if(prot_line_start>=prot_written)prot_len=prot_line_start;continue;}
  if(prot_len+2>=PROT_CAP)continue;
  prot_buf[prot_len++]=c;if(c=='\n')prot_line_start=prot_len;}
 return prot_orig->write_r(r,fd,ptr,len);
}
static void prot_note(const char *t){for(const char *p=t;*p&&prot_len+2<PROT_CAP;p++)prot_buf[prot_len++]=*p;if(prot_len+2<PROT_CAP){prot_buf[prot_len++]='\n';prot_line_start=prot_len;}}
static void prot_start(const char *name){
 char st[32];time_t t=time(NULL);
 if(t>1230768000){struct tm tm;gmtime_r(&t,&tm);snprintf(st,sizeof(st),"%04d-%02d-%02d_%02d-%02d-%02d",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);}
 else snprintf(st,sizeof(st),"ohne-Uhrzeit_%lu",(unsigned long)(gettime()/40500000ULL));
 snprintf(prot_path,sizeof(prot_path),"sd:/wii/protokoll/%s-%s.txt",name,st);
 if(devoptab_list[STD_OUT]&&!prot_orig){prot_orig=devoptab_list[STD_OUT];prot_dev=*prot_orig;prot_dev.write_r=prot_write;devoptab_list[STD_OUT]=&prot_dev;}
}
/* haengt neue Zeilen an die Protokolldatei; already_mounted=1: "sd:" ist gerade eingehaengt; 0: kurz ein- und wieder aushaengen */
static int prot_flush_sd(int already_mounted){
 if(!prot_path[0]||prot_written>=prot_len)return 0;
 int here=0;if(!already_mounted){if(!fatMountSimple("sd",&__io_wiisd))return -1;here=1;}
 mkdir("sd:/wii",0777);mkdir("sd:/wii/protokoll",0777);
 FILE *f=fopen(prot_path,"ab");int ok=0;unsigned n=prot_len-prot_written;
 if(f){ok=fwrite(prot_buf+prot_written,1,n,f)==n;if(fclose(f)!=0)ok=0;}
 if(here){fatUnmount("sd");__io_wiisd.shutdown();}
 if(ok)prot_written=prot_len;return ok?0:-1;
}
#include "espsignal.inc"
static void finish(const char *message) {
 puts(message);fflush(stdout);
 restore_config();
 { char t[200];snprintf(t,sizeof(t),"ERGEBNIS: %s",message);prot_note(t);fatUnmount("sd");__io_wiisd.shutdown();prot_flush_sd(0); }
 esp_led_signal('E'); /* ESP-LED blinkt: Lader ist abgebrochen */
 if(cfgfd>=0)ISFS_Close(cfgfd);
 puts(modified?"ACHTUNG: Original nicht zurueckgeschrieben! Sicherung auf der SD: sd:/wii/wlan-test/":"Test beendet. Wii kann ausgeschaltet werden.");
 for(;;)VIDEO_WaitVSync();
}
static u32 crc32(const u8 *p,unsigned n) {
 static u32 table[256];static int ready;
 if(!ready){for(unsigned i=0;i<256;i++){u32 c=i;for(int j=0;j<8;j++)c=c&1?(c>>1)^0xedb88320:c>>1;table[i]=c;}ready=1;}
 u32 c=~0u;while(n--)c=table[(c^*p++)&255]^(c>>8);return ~c;
}
#define PIPELINE 1
#define RB 4
#define RCH 32768
static u32 crc_table[256];static int crc_ready;
static u32 crc32_update(u32 c,const u8 *p,unsigned n){
 if(!crc_ready){for(unsigned i=0;i<256;i++){u32 x=i;for(int j=0;j<8;j++)x=x&1?(x>>1)^0xedb88320:x>>1;crc_table[i]=x;}crc_ready=1;}
 while(n--)c=crc_table[(c^*p++)&255]^(c>>8);return c;
}
static u8 ring[RB][RCH] ATTRIBUTE_ALIGN(32);
static mqbox_t q_full,q_free;
static u8 wstack[16384] ATTRIBUTE_ALIGN(32);
typedef struct{FILE *f;volatile int err;volatile unsigned written;}warg_t;
static void *writer_thread(void *arg){
 warg_t *wa=(warg_t*)arg;
 for(;;){
  mqmsg_t m;MQ_Receive(q_full,&m,MQ_MSG_BLOCK);
  unsigned v=(unsigned)m,idx=v&15,len=v>>4;
  if(len==0)break;
  if(!wa->err){if(fwrite(ring[idx],1,len,wa->f)!=len)wa->err=1;else wa->written+=len;}
  MQ_Send(q_free,(mqmsg_t)idx,MQ_MSG_BLOCK);
 }
 return NULL;
}
static int wait_socket(int sock,short event,int ms) {
 struct pollsd p={.socket=sock,.events=event};return net_poll(&p,1,ms);
}
static int connect_esp(void) {
 int sock=net_socket(AF_INET,SOCK_STREAM,IPPROTO_IP);if(sock<0)return sock;
 net_fcntl(sock,F_SETFL,O_NONBLOCK);
 struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_len=sizeof(addr);addr.sin_port=htons(8080);inet_aton("192.168.4.1",&addr.sin_addr);
 int r=net_connect(sock,(struct sockaddr*)&addr,sizeof(addr));
 if(r<0&&wait_socket(sock,POLLOUT,10000)<=0){net_close(sock);return -1;}
 return sock;
}
#define MAXENT 1700
typedef struct{unsigned size;u32 crc;char name[16];char target[112];char desc[80];}entry_t;
static entry_t ent[MAXENT];static int nent;
static u8 rbuf[32768] ATTRIBUTE_ALIGN(32);
/* Datei vom ESP holen: in den RAM (mem) oder per Schreib-Thread in eine geoeffnete Datei. Liefert 0 bei Erfolg, Groesse und CRC32. */
static double fp_wtotal=0,fp_wdone=0,fp_wcur=0;   /* gewichtete Sekunden (0,35 s je Datei + Groesse / 0,85 MiB/s) */
static double fp_weight(unsigned size){return 0.35+(double)size/(0.85*1048576.0);}
static int fetch(const char *name,FILE *file,u8 *mem,unsigned memcap,unsigned expect_size,u32 *crc_out,unsigned *size_out){
 int sock=connect_esp();if(sock<0){printf("TCP-Verbindung fehlgeschlagen: %d\n",sock);return -1;}
 char request[160];snprintf(request,sizeof(request),"GET /%s HTTP/1.0\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n",name);
 int len=strlen(request);if(net_send(sock,request,len,0)!=len){net_close(sock);return -1;}
 char header[512]={0};unsigned h=0;
 while(h<sizeof(header)-1){int n=net_recv(sock,header+h,1,0);if(n!=1){if(wait_socket(sock,POLLIN,10000)<=0)break;n=net_recv(sock,header+h,1,0);if(n!=1)break;}h++;if(h>=4&&!memcmp(header+h-4,"\r\n\r\n",4))break;}
 if(strncmp(header,"HTTP/1.0 200",12)&&strncmp(header,"HTTP/1.1 200",12)){printf("ESP-Antwort fuer %s: %.60s\n",name,header);net_close(sock);return -1;}
 char *cl=strstr(header,"Content-Length: ");unsigned size=cl?strtoul(cl+16,NULL,10):0;
 if(!size||(expect_size&&size!=expect_size)||(mem&&size>memcap)){net_close(sock);printf("Unerwartete Groesse %u fuer %s\n",size,name);return -1;}
 unsigned count=0;u32 c=~0u;u64 t0=gettime(),tp=t0,tf=t0;int bad=0;
 if(file){
  warg_t wa={file,0,0};lwp_t wt;
  MQ_Init(&q_full,RB);MQ_Init(&q_free,RB);
  for(int i=0;i<RB;i++)MQ_Send(q_free,(mqmsg_t)i,MQ_MSG_BLOCK);
  LWP_CreateThread(&wt,writer_thread,&wa,wstack,sizeof(wstack),64);
  while(count<size&&!wa.err&&!bad){
   mqmsg_t m;MQ_Receive(q_free,&m,MQ_MSG_BLOCK);unsigned idx=(unsigned)m;unsigned fill=0;
   while(fill<RCH&&count+fill<size){
    unsigned want=RCH-fill;if(want>size-count-fill)want=size-count-fill;
    if(wait_socket(sock,POLLIN,15000)<=0){bad=1;break;}
    int n=net_recv(sock,ring[idx]+fill,want,0);if(n<=0){bad=1;break;}
    c=crc32_update(c,ring[idx]+fill,n);fill+=n;
   }
   if(fill){MQ_Send(q_full,(mqmsg_t)((fill<<4)|idx),MQ_MSG_BLOCK);count+=fill;}
   else MQ_Send(q_free,(mqmsg_t)idx,MQ_MSG_BLOCK);
   if(diff_msec(tf,gettime())>500){tf=gettime();if(fp_wtotal>0)fp_frac(1,(float)((fp_wdone+fp_wcur*((double)(count+fill)/(double)size))/fp_wtotal));}
   if(diff_msec(tp,gettime())>2000){tp=gettime();printf("\r  %u / %u MiB  %.2f MiB/s   ",count>>20,size>>20,(count/1048576.0)/(diff_msec(t0,gettime())/1000.0));fflush(stdout);}
  }
  MQ_Send(q_full,(mqmsg_t)15,MQ_MSG_BLOCK);LWP_JoinThread(wt,NULL);
  if(wa.err||wa.written!=count)bad=1;
  MQ_Close(q_full);MQ_Close(q_free);
 } else {
  while(count<size){
   unsigned want=size-count;if(want>32768)want=32768;
   if(wait_socket(sock,POLLIN,15000)<=0){bad=1;break;}
   int n=net_recv(sock,mem+count,want,0);if(n<=0){bad=1;break;}
   c=crc32_update(c,mem+count,n);count+=n;
  }
 }
 net_close(sock);
 unsigned ms=diff_msec(t0,gettime());if(!ms)ms=1;
 if(file&&size>=(1u<<20))printf("\r  %u MiB in %.1f s = %.2f MiB/s                    \n",count>>20,ms/1000.0,(count/1048576.0)/(ms/1000.0));
 *crc_out=~c;*size_out=count;
 return (bad||count!=size)?-1:0;
}
/* CRC einer Datei auf der Wii-SD; -1 wenn nicht lesbar */
static int file_crc(const char *path,unsigned *size_out,u32 *crc_out){
 FILE *f=fopen(path,"rb");if(!f)return -1;
 u32 c=~0u;unsigned total=0;size_t n;
 while((n=fread(rbuf,1,sizeof(rbuf),f))>0){c=crc32_update(c,rbuf,n);total+=n;}
 int err=ferror(f);fclose(f);if(err)return -1;
 *size_out=total;*crc_out=~c;return 0;
}
static u32 manifest_crc;
static int load_manifest(void){
 const unsigned cap=262144;u8 *mb=memalign(32,cap);if(!mb)return -1;u32 c;unsigned got;
 if(fetch("MANIFEST.TXT",NULL,mb,cap-1,0,&c,&got)!=0){free(mb);return -1;}
 mb[got]=0;{u32 mc=~0u;mc=crc32_update(mc,mb,got);manifest_crc=~mc;}nent=0;char *line=(char*)mb;
 while(line&&*line&&nent<MAXENT){
  char *e=strchr(line,'\n');if(e)*e=0;
  size_t ll=strlen(line);if(ll&&line[ll-1]=='\r')line[ll-1]=0;
  unsigned sz;unsigned cr;char nm[16];int off=0;
  if(sscanf(line,"%u %x %15s%n",&sz,&cr,nm,&off)==3){
   char *t=line+off;while(*t==' '||*t=='\t')t++;
   ent[nent].size=sz;ent[nent].crc=cr;strcpy(ent[nent].name,nm);
   ent[nent].desc[0]=0;
   { char *tab=strchr(t,'\t'); /* optionale dritte Spalte: Anzeigetext fuer den Bildschirm */
     if(tab){*tab=0;char *d=tab+1;while(*d==' '||*d=='\t')d++;if(*d&&strlen(d)<sizeof(ent[nent].desc))strcpy(ent[nent].desc,d);
             size_t tl=strlen(t);while(tl&&(t[tl-1]==' '||t[tl-1]=='\t'))t[--tl]=0;} }
   if(*t&&strlen(t)<sizeof(ent[nent].target))strcpy(ent[nent].target,t);else ent[nent].target[0]=0;
   nent++;
  }
  line=e?e+1:NULL;
 }
 free(mb);return nent>0?0:-1;
}
static void make_dirs(const char *path){ /* legt alle Ordner von "sd:/a/b/datei" an, merkt sich den letzten Ordner */
 static char last[128];char tmp[160];strncpy(tmp,path,sizeof(tmp)-1);tmp[sizeof(tmp)-1]=0;
 char *slash=strrchr(tmp,'/');if(!slash)return;*slash=0;
 if(!strcmp(tmp,last))return;
 for(char *q=tmp+4;*q;q++)if(*q=='/'){*q=0;mkdir(tmp,0777);*q='/';}
 mkdir(tmp,0777);strncpy(last,tmp,sizeof(last)-1);last[sizeof(last)-1]=0;
}
static int marker_ok(void){
 FILE *f=fopen("sd:/WIIPKG/LISTE.OK","rb");if(!f)return 0;
 char b[32]={0};size_t n=fread(b,1,31,f);fclose(f);unsigned v=0;
 return n>0&&sscanf(b,"%x",&v)==1&&v==manifest_crc;
}
static void marker_write(void){
 FILE *f=fopen("sd:/WIIPKG/LISTE.OK","wb");if(f){fprintf(f,"%08x\n",manifest_crc);fclose(f);}
}
static int file_size_is(const char *path,unsigned size){
 struct stat st;return stat(path,&st)==0&&(unsigned)st.st_size==size;
}
/* ---- SD-Selbstpruefung: passt die Karte (Groesse, Platz, beschreibbar)? Wenn nicht: ggf. automatisch FAT32 formatieren ---- */
#define SD_MIN_BYTES  1100000000ULL   /* kleiner kann das Setup (ca. 0,85 GB + Reserve, FAT32 mit 16-KiB-Clustern ab ca. 1,07 GB) nicht fassen; nach oben KEINE Grenze */
#define DUMP_BYTES    553649152ULL    /* NAND-Dump */
#define RESERVE_BYTES (64ULL<<20)
#define SD_WARTE_S    60              /* EIN Countdown (Sekunden), wenn auf eine andere Karte gewartet wird */
#define SD_FORMAT_S   20              /* Countdown vor dem automatischen Formatieren (RESET = abbrechen) */
enum { SD_OK=0, SD_NOCARD, SD_NOFS, SD_SIZE, SD_FREE, SD_FREEWII, SD_RO, SD_FOREIGN };
static unsigned long long missing_bytes(void){ /* Bytes der Manifest-Dateien, die auf der gemounteten SD noch fehlen */
 unsigned long long m=0;
 for(int i=0;i<nent;i++){
  size_t tl=strlen(ent[i].target);if(tl&&ent[i].target[tl-1]=='/')continue;
  char path[128];if(ent[i].target[0])snprintf(path,sizeof(path),"sd:/%s",ent[i].target);else snprintf(path,sizeof(path),"sd:/WIIPKG/%s",ent[i].name);
  if(!file_size_is(path,ent[i].size))m+=ent[i].size;
 }
 return m;
}
/* Pruefung auf gemounteter Karte: SD_OK / SD_FREE / SD_RO / SD_NOFS */
static int sd_check(int with_manifest,char *why,size_t wl){
 struct statvfs sv;
 if(statvfs("sd:/",&sv)!=0||sv.f_frsize==0){snprintf(why,wl,"Dateisystem nicht lesbar");return SD_NOFS;}
 unsigned long long freeb=(unsigned long long)sv.f_bfree*sv.f_frsize;
 unsigned long long need=DUMP_BYTES+RESERVE_BYTES;
 if(with_manifest){unsigned long long m=missing_bytes();need+=m+m/32;}
 if(freeb<need){snprintf(why,wl,"zu wenig freier Platz (%llu MB frei, %llu MB noetig)",freeb/1000000ULL,need/1000000ULL);return SD_FREE;}
 int ok=0;FILE *f=fopen("sd:/WIISD.TMP","wb"); /* Schreibtest */
 if(f){static u8 blk[4096];memset(blk,0xA5,sizeof(blk));ok=fwrite(blk,1,sizeof(blk),f)==sizeof(blk);if(fclose(f)!=0)ok=0;}
 if(ok){ok=0;f=fopen("sd:/WIISD.TMP","rb");if(f){static u8 rd[4096];ok=fread(rd,1,sizeof(rd),f)==sizeof(rd)&&rd[0]==0xA5&&rd[4095]==0xA5;fclose(f);}}
 remove("sd:/WIISD.TMP");
 if(!ok){snprintf(why,wl,"nicht beschreibbar (Schreibschutz-Schalter pruefen)");return SD_RO;}
 return SD_OK;
}
/* 1, wenn auf der gemounteten Karte schon Wii-Software liegt (dann NIEMALS formatieren). */
static int sd_has_wii_software(void){
 static const char *const marks[]={"WIIPKG","apps","wii","wad","WIISETUP.ID"};
 DIR *d=opendir("sd:/");if(!d)return -1;
 struct dirent *e;int found=0;errno=0;
 while((e=readdir(d))!=NULL){
  for(unsigned i=0;i<sizeof(marks)/sizeof(marks[0]);i++)if(!strcasecmp(e->d_name,marks[i])){found=1;break;}
  if(found)break;
 }
 int failed=errno!=0;if(closedir(d)!=0)failed=1;
 return found?1:failed?-1:0;
}
/* Anzahl "fremder" Eintraege im Hauptverzeichnis (Windows-/Mac-Verwaltungsordner zaehlen nicht: eine sonst leere Karte bleibt "leer"). */
static int sd_count_foreign(void){
 static const char *const ignore[]={"System Volume Information","$RECYCLE.BIN","RECYCLER",".Trashes",".Spotlight-V100",".fseventsd",".DS_Store","FOUND.000","WIISD.TMP"};
 DIR *d=opendir("sd:/");if(!d)return -1;
 int n=0;struct dirent *e;errno=0;
 while((e=readdir(d))!=NULL){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  if(e->d_name[0]=='.'&&e->d_name[1]=='_')continue;
  int skip=0;for(unsigned i=0;i<sizeof(ignore)/sizeof(ignore[0]);i++)if(!strcasecmp(e->d_name,ignore[i])){skip=1;break;}
  if(!skip)n++;
 }
 int failed=errno!=0;if(closedir(d)!=0)failed=1;return failed?-1:n;
}
/* Bewertet die eingelegte Karte. *sec = Groesse in Sektoren (0 = keine Karte). */
static int sd_eval(int with_manifest,char *why,size_t wl,u32 *sec){
 *sec=sdfmt_probe_sectors();
 if(*sec==0){snprintf(why,wl,"keine SD-Karte erkannt (noetig: FAT32, mind. 1,1 GB, besser 2 GB oder mehr)");return SD_NOCARD;}
 if(*sec==SDFMT_TOO_BIG){snprintf(why,wl,"2 TiB oder mehr wird nicht unterstuetzt");return SD_SIZE;}
 unsigned long long bytes=(unsigned long long)(*sec)*512ULL;
 if(bytes<SD_MIN_BYTES){snprintf(why,wl,"zu klein (%llu MB, mindestens %llu MB noetig)",bytes/1000000ULL,SD_MIN_BYTES/1000000ULL);return SD_SIZE;}
 int fsk=sdfmt_fs_kind();
 if(!fatMountSimple("sd",&__io_wiisd)){
  snprintf(why,wl,"Dateisystem nicht sicher lesbar - Wii-Software nicht ausschliessbar, wird NICHT formatiert");return SD_FREEWII;
 }
 int wii=sd_has_wii_software();
 if(wii<0){fatUnmount("sd");snprintf(why,wl,"Inhalt nicht sicher pruefbar - wird NICHT formatiert");return SD_FREEWII;}
 if(fsk!=1&&fsk!=0){
  fatUnmount("sd");snprintf(why,wl,"Dateisystem nicht FAT16/FAT32 - wird NICHT automatisch formatiert");return SD_FREEWII;
 }
 int r=sd_check(with_manifest,why,wl);
 if(r==SD_FREE&&wii){
  size_t n=strlen(why);snprintf(why+n,wl-n," - Karte enthaelt schon Wii-Software, wird NICHT formatiert");r=SD_FREEWII;
 }else if(r==SD_OK&&!wii){            /* passt vom Platz, aber Fremddateien und keine Wii-Software -> sauber formatieren */
  int nf=sd_count_foreign();
  if(nf<0){snprintf(why,wl,"Verzeichnis nicht lesbar - wird NICHT formatiert");r=SD_FREEWII;}
  else if(nf>0){snprintf(why,wl,"enthaelt Fremddateien (%d Eintraege im Hauptverzeichnis) und keine Wii-Software",nf);r=SD_FOREIGN;}
 }
 fatUnmount("sd");
 return r;
}
/* Countdown + Formatieren. 1 = formatiert, 0 = RESET (abgebrochen), -1 = Karte gewechselt/entfernt, -2 = Fehler */
static int sd_format_countdown(int with_manifest,int st0,u32 sec,const char *why){
 unsigned char expected_id[16];if(!sdfmt_card_id(expected_id))return -1;
 printf("\nSD-Karte (%llu MB): %s.\nSie wird jetzt automatisch als FAT32 FORMATIERT.\n*** ALLE DATEN AUF DER KARTE GEHEN VERLOREN! ***\nRESET-Taste an der Konsole = ABBRECHEN.\n",(unsigned long long)sec*512ULL/1000000ULL,why);
 for(int left=SD_FORMAT_S;left>0;left--){
  printf("\r  Formatierung in %2d s ...   ",left);fflush(stdout);
  for(int i=0;i<10;i++){fp_tick();if(SYS_ResetButtonDown()){printf("\r%-48s\nFormatieren abgebrochen (RESET).\n","");fflush(stdout);return 0;}usleep(100000);}
  { /* jede Sekunde den ZUSTAND neu bewerten: Karte getauscht (auch gegen eine gute gleicher Groesse) -> NICHT formatieren */
   char w2[160];u32 sec2=0;int st2=sd_eval(with_manifest,w2,sizeof(w2),&sec2);
   unsigned char id2[16];
   if(st2!=st0||sec2!=sec||!sdfmt_card_id(id2)||memcmp(id2,expected_id,16)){printf("\r%-48s\nKarte entfernt oder gewechselt - pruefe neu ...\n","");fflush(stdout);return -1;}
  }
 }
 { /* letzte Absicherung unmittelbar vor dem Formatieren */
  char w2[160];u32 sec2=0;int st2=sd_eval(with_manifest,w2,sizeof(w2),&sec2);
  unsigned char id2[16];
   if(st2!=st0||sec2!=sec||!sdfmt_card_id(id2)||memcmp(id2,expected_id,16)){printf("\r%-48s\nKarte hat sich geaendert - es wird NICHT formatiert, pruefe neu ...\n","");fflush(stdout);return -1;}
 }
 printf("\r%-48s\nFormatiere als FAT32 (bei sehr grossen Karten kann das 1-2 Minuten dauern) ...\n","");fflush(stdout);
 char msg[96];int r=sdfmt_format(sec,expected_id,msg,sizeof(msg));
 if(r!=0){printf("Formatieren FEHLGESCHLAGEN: %s\n",msg);fflush(stdout);return -2;}
 puts("Formatiert. Pruefe die Karte neu ...");fflush(stdout);return 1;
}
/* Sorgt dafuer, dass eine passende Karte steckt: passt sie nicht, wird sie (wenn Groesse ok) nach Countdown formatiert;
 * sonst EIN sichtbarer Countdown (SD_WARTE_S s), jede Sekunde neue Pruefung, bei passender Karte geht es SOFORT weiter. */
static void sd_ensure_fit(int with_manifest){
 fp_pause(1);
 char why[160],shown[160];shown[0]=0;
 u32 refused=0,failed_fmt=0;int timer_on=0;u64 t0=0;
 for(;;){
  u32 sec=0;int st=sd_eval(with_manifest,why,sizeof(why),&sec);
  if(st==SD_OK){__io_wiisd.shutdown();if(timer_on)printf("\r%-48s\n","");puts("SD-Karte passt (Groesse, Platz, beschreibbar). Es geht sofort weiter.");fflush(stdout);fp_pause(0);return;}
  int fixable=(st==SD_FREE||st==SD_FOREIGN)&&sec!=0&&sec!=SDFMT_TOO_BIG&&sec!=refused&&sec!=failed_fmt;
  if(fixable){
   if(timer_on){printf("\r%-48s\n","");timer_on=0;}
   int r=sd_format_countdown(with_manifest,st,sec,why);
   if(r==1)continue;                 /* neu bewerten */
   if(r==-1)continue;
   if(r==0)refused=sec;else failed_fmt=sec;
   shown[0]=0;                       /* Hinweis fuer den Wartemodus neu zeigen */
  }
  if(!timer_on){timer_on=1;t0=gettime();}
  if(strcmp(why,shown)){snprintf(shown,sizeof(shown),"%s",why);printf("\r%-48s\nSD-Karte passt NICHT: %s.\nBitte eine passende SD-Karte einlegen - sobald sie steckt, geht es SOFORT weiter.\n","",why);fflush(stdout);}
  long left=(long)SD_WARTE_S-(long)(diff_msec(t0,gettime())/1000);
  if(left<=0)break;
  printf("\r  Noch %2ld s bis zum Abbruch ...   ",left);fflush(stdout);fp_tick();
  sleep(1);
 }
 printf("\r%-48s\n","");
 finish("Keine passende SD-Karte gefunden (Zeit abgelaufen). Es wurde nichts am System veraendert.");
}
/* ---- Gesamt-Fortschritt: Zustandsdatei auf der Wii-SD (fuer Installer/Dumper) ---- */
static int fp_loader_io(char *buf,int n,int write){
 FILE *f=fopen("sd:/WIIPKG/LAUF.TXT",write?"wb":"rb");if(!f)return -1;
 int r=write?(int)fwrite(buf,1,n,f):(int)fread(buf,1,n,f);
 fclose(f);return r;
}
static char done_flag[MAXENT];
static int download_all(void){
 int tries=0;while(!fatMountSimple("sd",&__io_wiisd)){if(++tries>60){puts("Keine SD.");return -1;}sleep(1);}
 mkdir("sd:/WIIPKG",0777);
 { /* Programme, die der Installer am Ende versteckt hat (apps-gesichert), zurueckholen, bevor Dateien geprueft werden */
   DIR *dd=opendir("sd:/apps-gesichert");
   if(dd){int n=0,sk=0;struct dirent *de;mkdir("sd:/apps",0777);
    while((de=readdir(dd))){if(de->d_name[0]=='.')continue;char a[220],b[220];snprintf(a,sizeof(a),"sd:/apps-gesichert/%s",de->d_name);snprintf(b,sizeof(b),"sd:/apps/%s",de->d_name);
     struct stat st;if(stat(b,&st)==0){sk++;continue;}if(rename(a,b)==0)n++;else sk++;}
    closedir(dd);rmdir("sd:/apps-gesichert");printf("Versteckte Programme zurueckgeholt: %d (nicht verschoben: %d)\n",n,sk);fflush(stdout);} }
 memset(done_flag,0,sizeof(done_flag));
 int quick=marker_ok();if(quick)puts("Schnellpruefung: Markierung passt, es wird nur nach Dateien/Groessen geschaut.");
 unsigned long long tot=0;for(int i=0;i<nent;i++)tot+=ent[i].size;
 printf("Dateien laut Liste: %d, %llu MiB\n",nent,tot>>20);fflush(stdout);
 { /* Aeltere Fassungen des Rueckbau-Programms unter altem Namen entfernen, damit nur EINE (die neue) Datei uebrig bleibt */
   static const char *alt[]={"sd:/Saemtliche Software mods entfernen.dol","sd:/S\xc3\xa4mtliche Software mods entfernen.dol"};
   for(unsigned k=0;k<sizeof(alt)/sizeof(alt[0]);k++){struct stat st;if(stat(alt[k],&st)==0&&remove(alt[k])==0){printf("Alte Programmdatei entfernt: %s\n",alt[k]+4);fflush(stdout);}}
 }
 fp_wtotal=0;fp_wdone=0;fp_wcur=0;for(int i=0;i<nent;i++){size_t tl=strlen(ent[i].target);if(!(tl&&ent[i].target[tl-1]=='/'))fp_wtotal+=fp_weight(ent[i].size);}
 fp_phase(1);
 int ok_all=1;int skipped=0,loaded=0,failed=0,dirs=0;u64 tstart=gettime();
 for(int i=0;i<nent;i++){
  char path[128],part[132];
  fp_wcur=fp_weight(ent[i].size);if(fp_wtotal>0)fp_frac(1,(float)(fp_wdone/fp_wtotal));
  if(ent[i].target[0])snprintf(path,sizeof(path),"sd:/%s",ent[i].target);else snprintf(path,sizeof(path),"sd:/WIIPKG/%s",ent[i].name);
  snprintf(part,sizeof(part),"%s.part",path);
  const int big=ent[i].size>=(1u<<20);
  const char *label=ent[i].desc[0]?ent[i].desc:(ent[i].target[0]?ent[i].target:ent[i].name);
  if(big){printf("\n[%d/%d] %s (%u MiB)\n",i+1,nent,label,ent[i].size>>20);fflush(stdout);}
  else{printf("\r[%d/%d] geladen %d, uebersprungen %d, Fehler %d   ",i+1,nent,loaded,skipped,failed);fflush(stdout);}
  { size_t tl=strlen(ent[i].target);
    if(tl&&ent[i].target[tl-1]=='/'){ /* leerer Ordner */
     char dp[128];snprintf(dp,sizeof(dp),"sd:/%s",ent[i].target);dp[strlen(dp)-1]=0;
     char pp[140];snprintf(pp,sizeof(pp),"%s/x",dp);make_dirs(pp);mkdir(dp,0777);dirs++;continue;}
  }
  make_dirs(path);
  if(quick&&file_size_is(path,ent[i].size)){skipped++;fp_wtotal-=fp_wcur;continue;}
  unsigned sz=0;u32 cr=0;
  if(file_crc(path,&sz,&cr)==0&&sz==ent[i].size&&cr==ent[i].crc){if(big)puts("  schon vorhanden und geprueft - uebersprungen");skipped++;fp_wtotal-=fp_wcur;continue;}
  if(!big&&ent[i].desc[0]){printf("\n[%d/%d] %s (%u KiB)\n",i+1,nent,label,(ent[i].size+1023)>>10);fflush(stdout);}
  remove(path);remove(part);
  FILE *f=fopen(part,"wb");if(!f){printf("\n  Datei nicht anlegbar: %s\n",path);ok_all=0;failed++;continue;}
  int r=fetch(ent[i].name,f,NULL,0,ent[i].size,&cr,&sz);
  int cl=fclose(f);
  if(r!=0||cl!=0||cr!=ent[i].crc){printf("\n  FEHLER %s (r %d, close %d, CRC %08x erwartet %08x)\n",ent[i].name,r,cl,cr,ent[i].crc);remove(part);ok_all=0;failed++;continue;}
  if(rename(part,path)!=0){printf("\n  Umbenennen fehlgeschlagen: %s\n",path);ok_all=0;failed++;continue;}
  fp_wdone+=fp_wcur;loaded++;done_flag[i]=1;if(big){puts("  geladen, Empfangs-CRC OK");fflush(stdout);}
 }
 if(loaded==0&&failed==0)fp_skip(1);else fp_frac(1,1.0f);
 printf("\nFertig: %d geladen, %d uebersprungen, %d Fehler, Dauer %.1f min (Ordner angelegt: %d)\n",loaded,skipped,failed,diff_msec(tstart,gettime())/60000.0,dirs);
 /* Rueckleseprobe nach Aushaengen/Einhaengen */
 fatUnmount("sd");__io_wiisd.shutdown();
 if(!fatMountSimple("sd",&__io_wiisd)){puts("SD nicht neu einhaengbar");return -1;}
 int vok=1,checked=0,bad=0;
 fp_phase(2);int rtotal=0;for(int i=0;i<nent;i++)if(done_flag[i])rtotal++;
 for(int i=0;i<nent;i++){if(!done_flag[i])continue;char path[128];if(ent[i].target[0])snprintf(path,sizeof(path),"sd:/%s",ent[i].target);else snprintf(path,sizeof(path),"sd:/WIIPKG/%s",ent[i].name);unsigned sz;u32 cr;
  int r=file_crc(path,&sz,&cr);int good=(r==0&&sz==ent[i].size&&cr==ent[i].crc);checked++;
  if(!good){printf("Rueckgelesen %s: FEHLER\n",ent[i].name);vok=0;bad++;}
  fp_frac(2,rtotal?(float)checked/(float)rtotal:1.0f);
  if((checked&63)==0){printf("\rRuecklesen: %d geprueft, %d Fehler   ",checked,bad);fflush(stdout);}}
 if(rtotal==0)fp_skip(2);else fp_frac(2,1.0f);
 printf("\rRuecklesen: %d neu geladene Dateien geprueft, %d Fehler        \n",checked,bad);
 prot_flush_sd(1);
 if(ok_all&&vok)marker_write();
 fatUnmount("sd");__io_wiisd.shutdown();
 return (ok_all&&vok)?0:-1;
}

__asm__(
".text\n"
".balign 4\n"
".globl stub_begin\n"
"stub_begin:\n"
"  mr 31,5\n"
"1: cmpwi 4,0\n"
"  beq 9f\n"
"  lwz 6,0(3)\n"
"  lwz 7,4(3)\n"
"  lwz 8,8(3)\n"
"  lwz 9,12(3)\n"
"  addi 3,3,16\n"
"  addi 4,4,-1\n"
"  mr 10,6\n"
"  cmpwi 8,0\n"
"  beq 3f\n"
"  mtctr 8\n"
"2: lbz 11,0(7)\n"
"  addi 7,7,1\n"
"  stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 2b\n"
"3: subf 8,8,9\n"
"  cmpwi 8,0\n"
"  beq 5f\n"
"  mtctr 8\n"
"  li 11,0\n"
"4: stb 11,0(10)\n"
"  addi 10,10,1\n"
"  bdnz 4b\n"
"5: mr 10,6\n"
"  add 12,6,9\n"
"6: dcbf 0,10\n"
"  sync\n"
"  icbi 0,10\n"
"  addi 10,10,32\n"
"  cmplw 10,12\n"
"  blt 6b\n"
"  b 1b\n"
"9: sync\n"
"  isync\n"
"  mtctr 31\n"
"  bctr\n"
".globl stub_end\n"
"stub_end:\n"
);
extern char stub_begin[], stub_end[];

typedef struct{u8 id[16];u16 type,machine;u32 version,entry,phoff,shoff,flags;u16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx;}ehdr_t;
typedef struct{u32 type,offset,vaddr,paddr,filesz,memsz,flags,align;}phdr_t;
#define NB_BUF ((u8 *)0x91000000)
#define NB_CAP 0xE00000UL
/* Runner (DEV.ELF) von der Wii-SD lesen, pruefen und starten; kehrt nur bei Fehler zurueck. */
static void launch_runner(void){
 puts("Starte den Installer (DEV.ELF) von der Wii-SD ...");fflush(stdout);
 net_deinit();
 int tries=0;while(!fatMountSimple("sd",&__io_wiisd)){if(++tries>60){puts("Keine SD.");return;}sleep(1);}
 prot_note("ERGEBNIS: Pakete geladen und geprueft, Installer wird gestartet.");prot_flush_sd(1);
 fp_set_io(fp_loader_io);fp_phase(3);   /* Gesamt-Fortschritt: Zustand fuer Installer/Dumper auf die SD schreiben */
 FILE *f=fopen("sd:/WIIPKG/DEV.ELF","rb");if(!f){puts("DEV.ELF fehlt.");fatUnmount("sd");__io_wiisd.shutdown();return;}
 unsigned long size=fread(NB_BUF,1,NB_CAP,f);int fe=ferror(f);fclose(f);
 fatUnmount("sd");__io_wiisd.shutdown();
 if(fe||size<64){puts("DEV.ELF nicht lesbar.");return;}
 const ehdr_t *eh=(const ehdr_t *)NB_BUF;
 if(eh->id[0]!=0x7f||eh->id[1]!='E'||eh->id[2]!='L'||eh->id[3]!='F'||eh->id[4]!=1||eh->id[5]!=2||eh->machine!=20||eh->phentsize!=sizeof(phdr_t)||eh->phoff>size||eh->phnum>(size-eh->phoff)/sizeof(phdr_t)){puts("DEV.ELF ist kein gueltiges ELF.");return;}
 u32 *tab=(u32 *)0x91F10000;unsigned segs=0;int entry_valid=0;
 for(unsigned i=0;i<eh->phnum;i++){
  const phdr_t *ph=(const phdr_t *)(NB_BUF+eh->phoff+i*sizeof(phdr_t));
  if(ph->type!=1||ph->memsz==0)continue;
  if(ph->vaddr<0x80003100||ph->vaddr>=0x81800000||ph->memsz>0x81800000-ph->vaddr||ph->filesz>ph->memsz||ph->offset>size||ph->filesz>size-ph->offset||segs>=8){puts("DEV.ELF: ungueltiges Segment.");return;}
  if((ph->flags&1)&&eh->entry>=ph->vaddr&&eh->entry-ph->vaddr<ph->filesz)entry_valid=1;
  tab[segs*4+0]=ph->vaddr;tab[segs*4+1]=(u32)(NB_BUF+ph->offset);tab[segs*4+2]=ph->filesz;tab[segs*4+3]=ph->memsz;segs++;
 }
 if(!entry_valid){puts("DEV.ELF: ungueltiger Einstieg.");return;}
 printf("Installer geladen (%lu Byte, %u Segmente). Start ...\n",size,segs);fflush(stdout);usleep(500000);
 u32 stubsz=(u32)(stub_end-stub_begin);
 memcpy((void *)0x91F00000,stub_begin,stubsz);DCFlushRange((void *)0x91F00000,stubsz);ICInvalidateRange((void *)0x91F00000,stubsz);
 DCFlushRange(tab,segs*16);DCFlushRange(NB_BUF,(u32)size);
 IRQ_Disable();
 ((void (*)(u32 *,u32,u32))0x91F00000)(tab,segs,eh->entry);
}
int main(void) {
 VIDEO_Init();consoleInit(&console);consoleSetFont(&console,&the_cool_font);consoleSetWindow(&console,2,2,console.con_cols-2,console.con_rows-2-FP_RESERVED_PX/16);
 {GXRModeObj *rm=VIDEO_GetPreferredMode(NULL);fp_setup(FP_TABLE_SETUP,console.destbuffer,rm->fbWidth,rm->xfbHeight,rm->fbWidth*VI_DISPLAY_PIX_SZ,NULL,1);fp_phase(0);}
 prot_start("Lader");prot_note("=== Protokoll: WLAN-Lader (ESP -> Wii-SD), Version V3.4 ===");
 puts("WLAN-LADER - Pakete vom ESP auf die Wii-SD");printf("IOS %u rev %u\n",IOS_GetVersion(),IOS_GetRevision());
 puts("SD-Karte noetig: vorne in die Wii, FAT32, mind. 1,1 GB, besser 2 GB oder mehr (nach oben keine Grenze).");
 puts("Bitte Wii-SD vorne einstecken (wird fuer die Sicherung gebraucht).");fflush(stdout);
 s32 r=ISFS_Initialize();printf("ISFS_Initialize: %d\n",r);if(r<0)finish("ISFS nicht verfuegbar; nichts veraendert.");
 cfgfd=ISFS_Open(CFGPATH,ISFS_OPEN_RW);printf("config.dat oeffnen (RW): %d\n",cfgfd);
 if(cfgfd<0)finish("config.dat nicht schreibbar; nichts veraendert.");
 r=cfg_read(orig);if(r<0){printf("Lesen: %d\n",r);finish("config.dat nicht lesbar; nichts veraendert.");}
 have_orig=1;
 printf("Original: Version %08x, Typ %u, Timeout %u, Slots %02x %02x %02x, CRC %08x\n",*(u32*)orig,orig[4],orig[5],orig[8],orig[8+2332],orig[8+2*2332],crc32(orig,7004));
 for(int i=0;i<3;i++){u8 *c=orig+8+i*2332;printf(" Slot %d: Flags %02x, SSID-Laenge %u, Verschluesselung %u\n",i,c[0],c[2021],c[2025]);}
 fflush(stdout);
 // Sicherung auf die Wii-SD und zurueckpruefen, bevor irgendetwas veraendert wird
 sd_ensure_fit(0); fp_restart_clock(); /* SD-Selbstpruefung vor jeder Aenderung (Groesse/Platz/beschreibbar, Wartezeit, Abbruch) */
 if(!fatMountSimple("sd",&__io_wiisd))finish("Keine SD: ohne Sicherung wird nichts veraendert.");
 mkdir("sd:/wii",0777);mkdir("sd:/wii/wlan-test",0777);
 char name[64];snprintf(name,sizeof(name),"sd:/wii/wlan-test/config-%08x.dat",crc32(orig,7004));
 FILE *f=fopen(name,"wb");int okb=0;
 if(f){okb=fwrite(orig,1,7004,f)==7004;if(fclose(f)!=0)okb=0;}
 if(okb){fatUnmount("sd");__io_wiisd.shutdown();if(fatMountSimple("sd",&__io_wiisd)){f=fopen(name,"rb");okb=0;if(f){okb=fread(chk,1,7004,f)==7004&&!memcmp(chk,orig,7004);fclose(f);}}else okb=0;}
 fatUnmount("sd");__io_wiisd.shutdown();
 printf("Sicherung %s: %s\n",name+3,okb?"OK (zurueckgelesen)":"FEHLER");
 if(!okb)finish("Sicherung fehlgeschlagen; nichts veraendert.");
 if(orig[8+2021]==strlen(TEST_SSID)&&!memcmp(orig+8+1988,TEST_SSID,strlen(TEST_SSID))){puts("Testprofil aus frueherem Lauf erkannt: als Original wird das leere Profil wiederhergestellt.");memset(orig,0,7004);}
 memcpy(cfg,orig,7004);
 for(int i=1;i<3;i++)cfg[8+i*2332]&=~0x80;
 memset(cfg+8,0,2332); /* Slot 0 komplett leeren: Reste eines vorhandenen Heim-Profils (z. B. manuelle DNS 8.8.8.8) duerfen die Verbindung zum ESP-WLAN nicht stoeren */
 cfg[4]=1;if(!cfg[5])cfg[5]=10;cfg[8]=0xa6;
 memset(cfg+8+1988,0,32);memset(cfg+8+2032,0,64);
 memcpy(cfg+8+1988,TEST_SSID,strlen(TEST_SSID));cfg[8+2021]=strlen(TEST_SSID);cfg[8+2025]=5;cfg[8+2029]=strlen(TEST_PASS);memcpy(cfg+8+2032,TEST_PASS,strlen(TEST_PASS));
 modified=1;r=cfg_write(cfg);int r2=r==0?cfg_read(chk):-1;
 printf("Testprofil schreiben: %d, zurueckgelesen: %s\n",r,(r==0&&r2==0&&!memcmp(chk,cfg,7004))?"OK":"FEHLER");fflush(stdout);
 if(r!=0)finish("Schreiben fehlgeschlagen.");
 ISFS_Close(cfgfd);cfgfd=-1;ISFS_Deinitialize();
 printf("IOS %u neu laden, damit die Netzwerkkonfiguration neu gelesen wird ...\n",IOS_GetVersion());fflush(stdout);
 r=IOS_ReloadIOS(IOS_GetVersion());printf("IOS neu geladen: %d, IOS %u rev %u\n",r,IOS_GetVersion(),IOS_GetRevision());fflush(stdout);sleep(2);
 puts("Verbinde automatisch mit dem ESP-WLAN ...");fflush(stdout);
 char ip[16],mask[16],gateway[16];
 for(int a=1;a<=12;a++){r=if_config(ip,mask,gateway,true,20);printf("Verbindungsversuch %d: %d\n",a,r);fflush(stdout);if(r>=0)break;net_deinit();sleep(3);}
 printf("Netzwerk: %d, IP %s\n",r,r>=0?ip:"-");fflush(stdout);if(r<0)finish("WLAN-Verbindung fehlgeschlagen; keine Geschwindigkeitsmessung.");
 if(load_manifest()!=0)finish("Liste MANIFEST.TXT nicht vom ESP erhalten.");
 sd_ensure_fit(1); /* genauer Platzbedarf (fehlende Dateien + NAND-Dump) */
 int dr=download_all();
 if(dr!=0)finish("PAKETE UNVOLLSTAENDIG - bitte erneut starten (Vorhandenes wird uebersprungen).");
 puts("ALLE PAKETE GELADEN UND GEPRUEFT.");fflush(stdout);
 restore_config();if(cfgfd>=0){ISFS_Close(cfgfd);cfgfd=-1;}
 if(modified)finish("WLAN-Konfiguration nicht zurueckgeschrieben - Installer wird nicht gestartet.");
 launch_runner();
 finish("Installer konnte nicht gestartet werden.");return 0;
}
