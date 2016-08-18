//Copyright 2015 <>< Charles Lohr, see LICENSE file.

#include "mem.h"
#include "c_types.h"
#include "user_interface.h"
#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "espconn.h"
#include "mystuff.h"
#include "ws2812_i2s.h"
#include "commonservices.h"
#include <mdns.h>
#include <mystuff.h>
#include <gpio.h>
#include <common.h>
#include <usb.h>

#define PORT 7777

#define procTaskPrio        0
#define procTaskQueueLen    1

static volatile os_timer_t some_timer;
uint8_t last_leds[512*3];
int last_led_count;


//int ICACHE_FLASH_ATTR StartMDNS();

void user_rf_pre_init(void)
{
	//nothing.
}

char * strcat( char * dest, char * src )
{
	return strcat(dest, src );
}

//Tasks that happen all the time.

os_event_t    procTaskQueue[procTaskQueueLen];


//Awkward example with use of control messages to get data to/from device.
uint8_t user_control[150];
int     user_control_length_acc; //From host to us.
int     user_control_length_ret; //From us to host.


void usb_handle_custom_control( int bmRequestType, int bRequest, int wLength, struct usb_internal_state_struct * ist )
{
	struct usb_endpoint * e = ist->ce;

	if( bmRequestType == 0x80 )
	{
		if( bRequest == 0xa7) //US TO HOST "in"
		{
			if( user_control_length_ret )
			{
				e->ptr_in = user_control;
				e->size_in = user_control_length_ret;
				if( wLength < e->size_in ) e->size_in = wLength;
				user_control_length_ret = 0;
			}
		}
	}

	if( bmRequestType == 0x00 )
	{
		if( bRequest == 0xa6 && user_control_length_acc == 0 ) //HOST TO US "out"
		{
			e->ptr_out = user_control;
			e->max_size_out = sizeof( user_control );
			if( e->max_size_out > wLength ) e->max_size_out = wLength;
			e->got_size_out = 0;
			e->transfer_done_ptr = &user_control_length_acc;
		}

	}

}

static void ICACHE_FLASH_ATTR procTask(os_event_t *events)
{
	CSTick( 0 );

	if( user_control_length_acc )
	{
		//printf( "\nGot: %s\n", usb_internal_state.user_control );
		int r = issue_command( user_control, sizeof( user_control )-1, user_control, user_control_length_acc );
		user_control_length_acc = 0;
		//printf( "%d/%s/%d\n", usb_internal_state.user_control_length_acc, usb_internal_state.user_control, r );
		if( r >= 0 )
			user_control_length_ret = r;
	}

	system_os_post(procTaskPrio, 0, 0 );
}


uint8_t my_ep1[4];
uint8_t my_ep2[8];
extern uint32_t usb_ramtable[31];

//Timer event.
static void ICACHE_FLASH_ATTR myTimer(void *arg)
{
	int i;

	//Send mouse and keyboard commands
	my_ep1[2] = 2;
//	my_ep2[2] ^= 8;  //Keyboard

	usb_internal_state.eps[1].ptr_in = my_ep1;
	usb_internal_state.eps[2].ptr_in = my_ep2;

	usb_internal_state.eps[1].size_in = sizeof( my_ep1 );
	usb_internal_state.eps[2].size_in = sizeof( my_ep2 );

	usb_internal_state.eps[1].place_in = 0;
	usb_internal_state.eps[2].place_in = 0;

	usb_internal_state.eps[1].send = 1;
	usb_internal_state.eps[2].send = 1;

	CSTick( 1 );
}


void ICACHE_FLASH_ATTR charrx( uint8_t c )
{
	//Called from UART.
}


volatile uint32_t my_table[] = { 0, (uint32_t)&PIN_IN, (uint32_t)&PIN_OUT_SET, (uint32_t)&PIN_OUT_CLEAR, 0xffff0000, 0x0000ffff };



#ifdef PROFILE
int time_ccount(void) 
{
        unsigned r;

/*	volatile unsigned a = 0xabcdef01;
        asm volatile ("testp:");
	a &= ~(1<<10);
*/

        asm volatile ("\
	\n\
\
intrs: \
	call0 my_func\n\
	j end\n\
\n\
end:\n\
\
	\n\
	sub %[out], a11, a9\n\
	" : [out] "=r"(r) : : "a9", "a10", "a11" );

        return r; //rsr a9, ccount //rsr a11, ccount
//	addi %[out], %[out], -1\n\
}
#endif

void user_init(void)
{
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U,FUNC_GPIO2);
    PIN_DIR_OUTPUT = _BV(2);
	PIN_OUT_SET = _BV(2);

	//ets_delay_us(200000);
	//uart0_sendStr("\r\n\033c" );
	uart0_sendStr("esp8266 test usb driver\r\n");
	system_update_cpu_freq( 80 );
//#define PROFILE
#ifdef PROFILE
	uint32_t k = 0x89abcdef;
	uint8_t * g  = (uint8_t*)&k;
 system_update_cpu_freq(160);
	my_table[0] = 5;
	printf( "%02x %02x %02x %02x\n", g[0], g[1], g[2], g[3] );
	uint32_t rr = time_ccount();
	printf( ":::::%d / %02x / %d\n", rr, rr, my_table[0] );
	system_restart();
	while(1);
#endif

//Uncomment this to force a system restore.
//	system_restore();

	CSSettingsLoad( 0 );
	CSPreInit();

	//Create additional socket, etc. here.

	CSInit();

	//Set GPIO16 for INput
	WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
		(READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1);     // mux configuration for XPD_DCDC and rtc_gpio0 connection

	WRITE_PERI_REG(RTC_GPIO_CONF,
		(READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0); //mux configuration for out enable

	WRITE_PERI_REG(RTC_GPIO_ENABLE,
		READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe);       //out disable

	SetServiceName( "espusb" );
	AddMDNSName( "cn8266" );
	AddMDNSName( "espusb" );
	AddMDNSService( "_http._tcp", "An ESP8266 Webserver", 80 );
	AddMDNSService( "_cn8266._udp", "ESP8266 Backend", 7878 );

	//Add a process
	system_os_task(procTask, procTaskPrio, procTaskQueue, procTaskQueueLen);

	//Timer example
	os_timer_disarm(&some_timer);
	os_timer_setfn(&some_timer, (os_timer_func_t *)myTimer, NULL);
	os_timer_arm(&some_timer, 100, 1);

	printf( "Boot Ok.\n" );

	usb_init();
 
	wifi_set_sleep_type(LIGHT_SLEEP_T);
	wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);

	system_os_post(procTaskPrio, 0, 0 );
}


//There is no code in this project that will cause reboots if interrupts are disabled.
void EnterCritical()
{
}

void ExitCritical()
{
}


