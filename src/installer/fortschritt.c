/* fortschritt.c - Teil von "Wii Automod (Installer)"
 * Copyright (C) 2026 Maarcelnk03
 *
 * Dieses Programm ist freie Software: Sie koennen es unter den Bedingungen der
 * GNU General Public License, Version 2 oder (nach Ihrer Wahl) jeder spaeteren
 * Version, weitergeben und/oder veraendern. Siehe LICENSES/GPL-2.0.txt.
 *
 * Die Veroeffentlichung erfolgt in der Hoffnung, dass es nuetzlich ist, aber
 * OHNE JEDE GEWAEHRLEISTUNG. Siehe die GNU General Public License fuer Details.
 */
/* Gesamt-Fortschrittsanzeige (Fusszeile mit gruenem Balken + Restzeit). Siehe fortschritt.h. */
#include "fortschritt.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef FP_HOST
#include <gccore.h>
#else
#include <stdint.h>
typedef uint32_t u32;typedef uint8_t u8;
#define DCFlushRange(a,b) ((void)0)
#endif
#include "fp_font.inc"
#ifdef FP_HOST
extern long fp_host_time(void);
#define FP_NOW() fp_host_time()
#else
#define FP_NOW() ((long)time(NULL))
#endif

struct ph{const char *name;int sec;int kind;};
/* Soll-Dauern in Sekunden aus der Aufzeichnung vom 08.11.2026 (Standard-3,7-GB-Karte, Gesamt 24:20) */
static const struct ph T_SETUP[]={
 {"WLAN verbinden",26},{"Dateien laden",650,1},{"Dateien pruefen",62,2},{"Kernpaket schreiben",80,2},{"NAND-Sicherung",293,2},
 {"NAND-Sicherung pruefen",295,2},{"Kernpaket pruefen",16,2},{"Titel installieren",24},{"Priiloader",6},{"Neustart",8}};
static const struct ph T_REMOVE[]={
 {"Vorpruefung",3},{"Countdown",30},{"Titel loeschen",8},{"Priiloader entfernen",6},{"Neustart",3}};
static const struct ph *tab=T_SETUP;static int nph=10,total_sec=1460;

/* Getrennte Messwerte: Installation/Setup, Funk, SD. Wartezeiten liefern keine Tempomessung. */
static struct{long start,last,seconds[3];int cur,paused;unsigned done,skip;}st;
static float curfrac;
static unsigned char *fb;static int W,H,STR;static fp_io_fn io;
static long last_draw=-1;static int last_pct=-1,last_cur=-1;
static void account(void){
 long now=FP_NOW(),dt=now-st.last;
 if(dt>0&&!st.paused)st.seconds[tab[st.cur].kind]+=dt;
 st.last=now;
}
static void save(void){
 char b[256];account();
 int n=snprintf(b,sizeof(b),"FP2 t=%d start=%ld last=%ld cur=%d done=%u skip=%u pause=%d s=%ld,%ld,%ld\n",(int)(tab==T_REMOVE),st.start,st.last,st.cur,st.done,st.skip,st.paused,st.seconds[0],st.seconds[1],st.seconds[2]);
 if(io&&n>0&&n<(int)sizeof(b))io(b,n,1);
}
static int load(int table){
 char b[256];if(!io)return 0;
 int n=io(b,(int)sizeof(b)-1,0);if(n<=0||n>=(int)sizeof(b))return 0;b[n]=0;
 int t=-1;
 if(sscanf(b,"FP2 t=%d start=%ld last=%ld cur=%d done=%u skip=%u pause=%d s=%ld,%ld,%ld",&t,&st.start,&st.last,&st.cur,&st.done,&st.skip,&st.paused,&st.seconds[0],&st.seconds[1],&st.seconds[2])!=10)return 0;
 if(t!=table||st.cur<0||st.cur>=nph||st.start<=0||st.last<st.start||st.last>FP_NOW()||FP_NOW()-st.start>21600)return 0;
 if((st.done|st.skip)>=(1u<<nph)||(st.done&st.skip)||st.paused<0||st.paused>1)return 0;
 for(int k=0;k<3;k++)if(st.seconds[k]<0||st.seconds[k]>21600)return 0;
 return 1;
}

/* ---- Zeichnen (YCbYCr, 2 Pixel pro 32-Bit-Wort; alle x-Werte gerade) ---- */
static void rect(int x0,int y0,int w,int h,int Y,int Cb,int Cr){
 if(!fb||w<=0||h<=0)return;
 u32 word=((u32)Y<<24)|((u32)Cb<<16)|((u32)Y<<8)|(u32)Cr;
 for(int y=y0;y<y0+h&&y<H;y++){u32 *p=(u32 *)(fb+(long)y*STR)+x0/2;for(int i=0;i<w/2;i++)p[i]=word;}
}
static void text(int x,int y,const char *s,int fgY,int bgY){
 for(;*s;s++,x+=8){
  int c=(unsigned char)*s;if(c<32||c>126)c='?';
  const unsigned char *g=fp_font[c-32];
  for(int r=0;r<16;r++){
   u32 *p=(u32 *)(fb+(long)(y+r)*STR)+x/2;unsigned char bits=g[r];
   for(int k=0;k<4;k++){
    int a=(bits>>(7-2*k))&1,b=(bits>>(6-2*k))&1;
    p[k]=((u32)(a?fgY:bgY)<<24)|(128u<<16)|((u32)(b?fgY:bgY)<<8)|128u;
   }
  }
 }
}
static void calc(int *pct,long *el,long *eta){
 account();
 long e=FP_NOW()-st.start;if(e<0)e=0;
 float work[3]={0},remain[3]={0},done=0,total=0;
 for(int p=0;p<nph;p++){
  if(st.skip&(1u<<p))continue;
  float f=(st.done&(1u<<p))?1.0f:(p==st.cur?curfrac:0.0f);
  work[tab[p].kind]+=f*tab[p].sec;
  remain[tab[p].kind]+=(1.0f-f)*tab[p].sec;
  done+=f*tab[p].sec;total+=tab[p].sec;
 }
 float rem=0;
 for(int k=0;k<3;k++){
  float factor=1.0f;
  /* Erst nach genug beobachteter Arbeit extrapolieren, niemals Funk auf SD uebertragen. */
  if(work[k]>=30.0f&&st.seconds[k]>=10){factor=(float)st.seconds[k]/work[k];if(factor<0.2f)factor=0.2f;if(factor>2.5f)factor=2.5f;}
  rem+=remain[k]*factor;
 }
 int p=total>0?(int)(100.0f*done/total):100;if(p>100)p=100;if(p<0)p=0;
 *pct=p;*el=e;*eta=(long)(rem+0.5f);
}
static void draw(int force){
 if(!fb)return;
 long now=FP_NOW();int pct;long el,eta;calc(&pct,&el,&eta);
 if(!force&&now==last_draw&&pct==last_pct&&st.cur==last_cur)return;
 last_draw=now;last_pct=pct;last_cur=st.cur;
 int top=H-96;                                   /* Fusszeile: y = H-72 .. H-20 (20 px Rand fuer Overscan) */
 rect(0,H-FP_RESERVED_PX,W&~1,FP_RESERVED_PX,16,128,128);              /* schwarz, bis zum unteren Bildrand */
 char line[128];
 snprintf(line,sizeof(line),"Gesamt %3d%%  %d/%d %s",pct,st.cur+1,nph,tab[st.cur].name);
 if((int)strlen(line)>(W-48)/8)line[(W-48)/8]=0;
 text(24,top+4,line,235,16);
 snprintf(line,sizeof(line),"vergangen %02ld:%02ld  noch ca. %02ld:%02ld%s",el/60,el%60,eta/60,eta%60,st.paused?" (Warten)":"");
 if((int)strlen(line)>(W-48)/8)line[(W-48)/8]=0;
 text(24,top+24,line,235,16);
 int bx=24,bw=(W-48)&~1,by=H-44,bh=20;          /* Balken */
 rect(bx,by,bw,bh,235,128,128);                  /* Rahmen weiss */
 rect(bx+2,by+2,bw-4,bh-4,50,128,128);           /* Hintergrund dunkelgrau */
 int fillw=((bw-4)*pct/100)&~1;
 if(fillw>0)rect(bx+2,by+2,fillw,bh-4,101,79,65);/* gruen (wie der Dumper-Balken) */
 DCFlushRange(fb+(long)(H-FP_RESERVED_PX)*STR,(unsigned)((FP_RESERVED_PX)*STR));
}

void fp_setup(int table,void *xfb,int width,int height,int stride_bytes,fp_io_fn iof,int fresh){
 fb=(unsigned char *)xfb;W=width;H=height;STR=stride_bytes;io=iof;
 tab=table==FP_TABLE_REMOVE?T_REMOVE:T_SETUP;nph=table==FP_TABLE_REMOVE?5:10;total_sec=0;for(int i=0;i<nph;i++)total_sec+=tab[i].sec;
 memset(&st,0,sizeof(st));curfrac=0;last_draw=-1;last_pct=-1;last_cur=-1;
 if(fresh||!load(table)){memset(&st,0,sizeof(st));st.start=st.last=FP_NOW();save();}
 draw(1);
}
void fp_phase(int id){
 if(id<0||id>=nph)return;
 account();
 if(id!=st.cur){if(!(st.skip&(1u<<st.cur)))st.done|=1u<<st.cur;st.cur=id;curfrac=0;}
 st.skip&=~(1u<<id);st.done&=~(1u<<id);save();draw(1);
}
void fp_skip(int id){
 if(id<0||id>=nph)return;
 account();st.skip|=1u<<id;st.done&=~(1u<<id);save();draw(1);
}
void fp_pause(int paused){account();st.paused=paused?1:0;draw(1);}
void fp_restart_clock(void){st.start=st.last=FP_NOW();memset(st.seconds,0,sizeof(st.seconds));draw(1);}
void fp_frac(int id,float f){
 if(id!=st.cur)return;
 if(f<0)f=0;
 if(f>1)f=1;
 curfrac=f;draw(0);
}
void fp_set_io(fp_io_fn iof){io=iof;}
void fp_save(void){save();}
void fp_tick(void){draw(0);}
void fp_finish(void){
 if(nph<1)return;
 account();st.done=((1u<<nph)-1)&~st.skip;st.cur=nph-1;curfrac=1;st.paused=1;save();draw(1);
}
