#ifndef LEDMARK_H
#define LEDMARK_H
/* Disc-Slot-LED der Wii als Diagnosekanal (sichtbar ohne Bild). Register wie libogc-WiiLight: 0xCD0000C0 Bit 5. */
static inline void led_raw(int on)
{
	volatile unsigned int *gpio = (volatile unsigned int *)0xCD0000C0;
	if (on) *gpio |= 0x20u; else *gpio &= ~0x20u;
}
static inline unsigned int led_tb(void) { unsigned int t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }
static inline void led_ms(unsigned int ms) { unsigned int s = led_tb(), need = ms * 60750u; while ((unsigned int)(led_tb() - s) < need) { } }
static inline void led_blink(int n) { for (int i = 0; i < n; ++i) { led_raw(1); led_ms(150); led_raw(0); led_ms(150); } led_ms(500); }
#endif
