#include "btstack.h"
#include "btstack_port_esp32.h"
#include "btstack_stdio_esp32.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
#include "driver/gpio.h"

#include "bluebomb_micro.h"

// warn about unsuitable sdkconfig
#include "sdkconfig.h"
#if !CONFIG_BT_ENABLED
#error "Bluetooth disabled - please set CONFIG_BT_ENABLED via menuconfig -> Component Config -> Bluetooth -> [x] Bluetooth"
#endif
#if !CONFIG_BT_CONTROLLER_ONLY
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ONLY via menuconfig -> Component Config -> Bluetooth -> Host -> Disabled"
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
#if !CONFIG_BT_CONTROLLER_ENABLED
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ENABLED via menuconfig -> Component Config -> Bluetooth -> Controller -> Enabled"
#endif
#endif
#ifndef ENABLE_CLASSIC
#error "Classic mode needs to be enabled for bluebomb to work"
#endif

#define BLINK_ENABLE CONFIG_BLINK_ENABLE
#define BLINK_GPIO CONFIG_BLINK_GPIO

// Provided by generated l2cb.c file
extern uint32_t bluebomb_l2cb;

extern volatile int bluebomb_done_count;
void bench_start(void);
void bench_prepare_sd(void);
static btstack_timer_source_t heartbeat;

/* ---- Rueckmeldung der Wii ueber USB-Seriell: "@K@" = Installer erfolgreich -> weisse Blitz-LED (GPIO4)
 *      dauerhaft AN; "@E@" = Fehler nach dem Hack -> LED BLINKT. Beides bis der ESP stromlos wird. ---- */
#define STATUS_LED GPIO_NUM_4
static btstack_timer_source_t status_blink;
static bool status_blink_active = false;
static bool status_blink_level = false;
static void status_blink_handler(struct btstack_timer_source *ts)
{
    status_blink_level = !status_blink_level;
    gpio_set_level(STATUS_LED, status_blink_level);
    btstack_run_loop_set_timer(ts, 250);
    btstack_run_loop_add_timer(ts);
}
static void status_led_prepare(void)
{
    gpio_reset_pin(STATUS_LED);
    gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
}
static void status_led_apply(char kind)
{
    if(kind=='K'){            /* Erfolg: Dauerlicht */
        if(status_blink_active){btstack_run_loop_remove_timer(&status_blink);status_blink_active=false;}
        status_led_prepare();
        gpio_set_level(STATUS_LED, 1);
    } else if(kind=='E'){     /* Fehler: Blinken */
        status_led_prepare();
        if(!status_blink_active){
            status_blink_active=true;
            status_blink.process=&status_blink_handler;
            btstack_run_loop_set_timer(&status_blink, 250);
            btstack_run_loop_add_timer(&status_blink);
        }
    }
}
static void bench_command(char c)
{
    static int pos=0;static char kind=0;
    if(c=='W'){pos=0;hci_power_control(HCI_POWER_OFF);bench_start();return;} /* 'W' ist nie Teil von @K@/@E@ */
    if(pos==0){
        if(c=='@')pos=1;
        return;
    }
    if(pos==1){
        if(c=='K'||c=='E'){kind=c;pos=2;}
        else pos=(c=='@')?1:0;
        return;
    }
    /* pos==2: abschliessendes '@' */
    pos=0;
    if(c=='@')status_led_apply(kind);
}

static void heartbeat_handler(struct btstack_timer_source *ts)
{
    static bool started=false;
    if(!started && bluebomb_done_count>0){started=true;hci_power_control(HCI_POWER_OFF);bench_start();}
    // Invert the led
    static bool led_on = true;
    led_on = !led_on;

#if BLINK_ENABLE
    gpio_set_level(BLINK_GPIO, led_on);
#endif

    uint32_t blink_speed;
    switch (bluebomb_get_stage()) {
        // Very slow idle blink
        case STAGE_INIT:
            blink_speed = 1000;
            break;
        // Fast blink while uploading payload
        case STAGE_UPLOAD_PAYLOAD:
            blink_speed = 50;
            break;
        // Medium blink during all other connected stages
        default:
            blink_speed = 100;
            break;
    }

    // Restart timer
    btstack_run_loop_set_timer(ts, blink_speed);
    btstack_run_loop_add_timer(ts);
}

int app_main(void)
{
    printf("\nWLAN-SPEEDTEST 1 / Wii 4.3E / automatisches ESP-WLAN\n");
#if BLINK_ENABLE
    // Set up the LED
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
#endif

    // optional: enable packet logger
    // hci_dump_init(hci_dump_embedded_stdout_get_instance());

    // Enable buffered stdout
    btstack_stdio_init();
    btstack_stdin_setup(bench_command);

    bench_prepare_sd();
    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // set one-shot btstack timer
    heartbeat.process = &heartbeat_handler;
    btstack_run_loop_set_timer(&heartbeat, 1000);
    btstack_run_loop_add_timer(&heartbeat);

    // Setup bluebomb
    bluebomb_setup(bluebomb_l2cb);

    // Enter run loop (forever)
    btstack_run_loop_execute();

    return 0;
}
