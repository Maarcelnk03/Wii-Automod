/* Fehler-/Erfolgssignal an den ESP (weisse Blitz-LED): 'E' = Blinken (Programm bricht ab), 'K' = Dauerlicht.
 * Sendet "@E@\n" bzw. "@K@\n" zweimal ueber den CH340 am Wii-USB-Port (115200 Baud, DTR/RTS inaktiv = kein ESP-Reset).
 * Best effort, blockiert nie laenger als ca. 4 s; ist kein ESP/CH340 angeschlossen, wird nur eine Zeile angezeigt. */
#include <ogc/usb.h>
static usb_device_entry esp_devs[16] ATTRIBUTE_ALIGN(32);
static u8 esp_txb[32] ATTRIBUTE_ALIGN(32);
static void esp_led_signal(char kind)
{
	s32 efd = -1;
	bool sent = false;
	if (USB_Initialize() >= 0) {
		for (unsigned attempt = 0; attempt < 12 && efd < 0; ++attempt) {
			u8 count = 0;
			if (USB_GetDeviceList(esp_devs, 16, 0, &count) < 0)
				break;
			for (unsigned i = 0; i < count; ++i)
				if (esp_devs[i].vid == 0x1a86 && esp_devs[i].pid == 0x7523) {
					if (USB_OpenDevice(esp_devs[i].device_id, esp_devs[i].vid, esp_devs[i].pid, &efd) < 0)
						efd = -1;
					break;
				}
			if (efd < 0)
				usleep(250000);
		}
		if (efd >= 0) {
			const char msg[4] = { '@', kind, '@', '\n' };
			USB_WriteCtrlMsg(efd, 0x40, 0xa1, 0, 0, 0, NULL);
			USB_WriteCtrlMsg(efd, 0x40, 0x9a, 0x1312, 0xcc83, 0, NULL);
			USB_WriteCtrlMsg(efd, 0x40, 0x9a, 0x2518, 0x00c3, 0, NULL);
			USB_WriteCtrlMsg(efd, 0x40, 0xa4, (u16)~0x00, 0, 0, NULL);
			for (int i = 0; i < 2; ++i) {
				memcpy(esp_txb, msg, 4);
				DCFlushRange(esp_txb, sizeof(esp_txb));
				USB_WriteBlkMsg(efd, 0x02, 4, esp_txb);
				usleep(150000);
			}
			sent = true;
			USB_CloseDevice(&efd);
		}
		USB_Deinitialize();
	}
	printf(sent ? "ESP-LED: Signal '%c' gesendet.\n" : "ESP-LED: Signal '%c' NICHT gesendet (ESP/CH340 nicht gefunden).\n", kind);
	fflush(stdout);
}
