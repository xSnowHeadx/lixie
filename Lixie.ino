extern "C"
{
#include "user_interface.h"  	  	// Required for wifi_station_connect() to work
}

#define _NO_NUMERIC_ORDER_	0		// define this to place the numbers not in numeric order to prevent the big position jump from 0 to 1

#define _FASTLED_ 0
#include <ESP8266WiFi.h>          	// https://github.com/esp8266/Arduino
#include <WiFiUdp.h>
#include <time.h>

#include <EEPROM.h>
#define FASTLED_ESP8266_RAW_PIN_ORDER
#include <FastLED.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <fastpin.h>
#include <fastspi.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>
#include <Adafruit_NeoPixel.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <NTPClient.h>			  // https://github.com/arduino-libraries/NTPClient
#include <Timezone.h>    		  // https://github.com/JChristensen/Timezone
#include <Ticker.h>
#include <OneButton.h>
#include <errno.h>

// begin of individual settings

#define HAS_COLONS		   0 	// set to 0 if no colon digits used

#define DATE_TIME		   4    // show date for 4 seconds

#define DATE_BUTTON		   0	// button on GPIO0 to show date

#if HAS_COLONS
#define NUM_LEDS 		 124
#define NUM_USED_LEDS	  16
#define NUM_DIGIT_LEDS	  20
#define NUM_COLON_LEDS	   2
#define NUM_DIGITS		   8
#else
#define NUM_LEDS 		 120
#define NUM_USED_LEDS	  12
#define NUM_DIGIT_LEDS	  20
#define NUM_COLON_LEDS	   0
#define NUM_DIGITS		   6
#endif

enum
{
	CMOD_NONE, CMOD_STEADY, CMOD_BLINK, CMOD_ALTERNATE, CMOD_AMPM, CMOD_DATE, CMOD_NUM_MODES,
};

enum
{
	DMOD_NONE, DMOD_MINUTE, DMOD_HOUR, DMOD_NOW
};

#define DATA_PIN 	 14			  // output-pin for LED-data (current D5)

#if _FASTLED_
#define COLOR_ORDER RGB			  // byte order in the LEDs data-stream, adapt to your LED-type
#else
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);
#endif
// configure your timezone rules here how described on https://github.com/JChristensen/Timezone
TimeChangeRule myDST =
{ "CEST", Last, Sun, Mar, 2, +120 };    //Daylight time = UTC + 2 hours
TimeChangeRule mySTD =
{ "CET", Last, Sun, Oct, 2, +60 };      //Standard time = UTC + 1 hours
Timezone myTZ(myDST, mySTD);

// end of individual settings

#define EEPROM_SIZE 128			  // size of NV-memory

#define FPM_SLEEP_MAX_TIME 0xFFFFFFF
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#define CONSTRAIN(a, l, r)    (MIN(MAX((l), (a)), (r)))

typedef struct
{
	float r;
	float g;
	float b;
} FRGB;

CRGB actcolor;

OneButton d_mode(DATE_BUTTON, true);			// create date button object

Ticker sweeper;

unsigned char synced = 0;
unsigned int datemode = 0;
unsigned int brightness = 100;

// display layout and led order

#if _NO_NUMERIC_ORDER_
// number order from front to back: 1 0 9 2 7 8 3 6 5 4
/*

 Number			LED position
 	 4       4    	   009---->|     019
 5 	 	5 	 	000     |     010     |
 	 6	 	6	 |	   008	   |	 018
 3	 	3	 	001	    |	  011	  |
 	 8	 	8	 |	   007	   |	 017
 7	 	7	 	002     |	  012	  |
 	 2	 	2	 |	   006	   |	 016
 9	 	9	 	003     |	  013	  |
 	 0	 	0	 |	   005	   |	 015
 1	 	1	 	004---->|     014---->|
 */

static const unsigned long ledmap[10] =
{
	0b00001000000000100000,
	0b00000100000000010000,
	0b00010000000001000000,
	0b00000000100000000010,
	0b10000000001000000000,
	0b00000000010000000001,
	0b01000000000100000000,
	0b00000001000000000100,
	0b00100000000010000000,
	0b00000010000000001000,
};

#else

// number order from front to back: 1 2 3 4 5 6 7 8 9 0
/*
 Number			LED position
 	 0       0    	   009---->|     019
 9 	 	9 	 	000     |     010     |
 	 8	 	8	 |	   008	   |	 018
 7	 	7	 	001	    |	  011	  |
 	 6	 	6	 |	   007	   |	 017
 5	 	5	 	002     |	  012	  |
 	 4	 	4	 |	   006	   |	 016
 3	 	3	 	003     |	  013	  |
 	 2	 	2	 |	   005	   |	 015
 1	 	1	 	004---->|     014---->|
 */
static const unsigned long ledmap[10] =
{
	0b10000000001000000000,
	0b00000100000000010000,
	0b00001000000000100000,
	0b00000010000000001000,
	0b00010000000001000000,
	0b00000001000000000100,
	0b00100000000010000000,
	0b00000000100000000010,
	0b01000000000100000000,
	0b00000000010000000001, };
#endif

static const float corr_digit[NUM_DIGIT_LEDS / 2] =
{
//      9	  7	    5	  3     1
		1.00, 1.0, 0.90, 0.80, 0.85,
//      2	  4	    6	  8     0
		0.75, 0.75, 0.94, 0.96, 1.00 };

#if HAS_COLONS
static const unsigned char colonmap[CMOD_NUM_MODES] =
{ 		0b00000000,	// CMOD_NOTHING
		0b00110011, // CMOD_STEADY
		0b00000011, // CMOD_BLINK
		0b00100001, // CMOD_ALTERNATE
		0b00001001, // CMOD_AMPM
		0b00010001, // CMOD_DATE
		};

static const float corr_colon[NUM_COLON_LEDS] =
{
//      COL-B COL-T
		0.70, 0.70 };
#endif

void update_timeleds(void);

enum
{
	COLORMODE_FIX, COLORMODE_RBFULL, COLORMODE_RBFIX, COLORMODE_RBSHIFT
};

// structure holds the parameters to save nonvolatile
typedef struct
{
	unsigned char h24;
	unsigned char mode;
	unsigned char fade;
	unsigned char cmode;
	unsigned char r, g, b;
	unsigned char brightness;
	unsigned char dmode;
	unsigned char nm_start;
	unsigned char nm_end;
	unsigned char nm_brightness;
	unsigned char valid;
} lixie_processor_struct;
lixie_processor_struct lixie_processor;
lixie_processor_struct *lixiep = &lixie_processor;

// float parameters for rainbow-mode
float rainbow_offset = 0.0, rainbow_step = 0.000006, rainbow_posstep = 1.0 / (float) NUM_DIGITS;

// LED-array for transfer to FastLED
CRGB leds[NUM_LEDS];
float leds_last[NUM_LEDS];
float leds_curr[NUM_LEDS];
float leds_step;

// WiFi-objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WiFiManager wifiManager;

char outstr[512] = "";
// string to hold the last displayed word-sequence
char tstr1[128], tstr2[128] = "";

// the web-server-object
ESP8266WebServer server(80);   //instantiate server at port 80 (http port)

// click function of date button
void do_dmode(void)
{
	datemode = DATE_TIME * 100;
}

// own fmod() because of issue in the libm.a ( see https://github.com/esp8266/Arduino/issues/612 )
float xfmod(float numer, float denom)
{
	int ileft;

	if (!denom)
		return numer;

	ileft = numer / denom;
	return numer - (ileft * denom);
}

int get_digit_pos(int dledpos)
{
	int rval = 0;

#if ! HAS_COLONS
	rval = dledpos / NUM_DIGIT_LEDS;
#else
	if (dledpos >= NUM_DIGIT_LEDS)
	{
		rval = 1;
		if (dledpos >= 2 * NUM_DIGIT_LEDS)
		{
			rval = 2;
			if (dledpos >= 2 * NUM_DIGIT_LEDS + NUM_COLON_LEDS)
			{
				rval = 3;
				if (dledpos >= 3 * NUM_DIGIT_LEDS + NUM_COLON_LEDS)
				{
					rval = 4;
					if (dledpos >= 4 * NUM_DIGIT_LEDS + NUM_COLON_LEDS)
					{
						rval = 5;
						if (dledpos >= 4 * NUM_DIGIT_LEDS + 2 * NUM_COLON_LEDS)
						{
							rval = 6;
							if (dledpos >= 5 * NUM_DIGIT_LEDS + 2 * NUM_COLON_LEDS)
							{
								rval = 7;
							}
						}
					}
				}
			}
		}
	}
#endif
	return rval;
}

// conversion from hsl- to RGB-color-scheme
void hsl_to_rgb(int hue, int sat, int lum, int* r, int* g, int* b)
{
	int v;

	v = (lum < 128) ? (lum * (256 + sat)) >> 8 : (((lum + sat) << 8) - lum * sat) >> 8;
	if (v <= 0)
	{
		*r = *g = *b = 0;
	}
	else
	{
		int m;
		int sextant;
		int fract, vsf, mid1, mid2;

		m = lum + lum - v;
		hue *= 6;
		sextant = hue >> 8;
		fract = hue - (sextant << 8);
		vsf = v * fract * (v - m) / v >> 8;
		mid1 = m + vsf;
		mid2 = v - vsf;
		switch (sextant)
		{
		case 0:
			*r = v;
			*g = mid1;
			*b = m;
			break;
		case 1:
			*r = mid2;
			*g = v;
			*b = m;
			break;
		case 2:
			*r = m;
			*g = v;
			*b = mid1;
			break;
		case 3:
			*r = m;
			*g = mid2;
			*b = v;
			break;
		case 4:
			*r = mid1;
			*g = m;
			*b = v;
			break;
		case 5:
			*r = v;
			*g = m;
			*b = mid2;
			break;
		}
	}
}

// generate next rainbow-color
void update_rainbow(unsigned char dpos)
{
	switch (lixiep->mode)
	{
	case COLORMODE_FIX:
	{
		// generate color-structure for wished display-color
		actcolor = CRGB(((int) lixiep->r * brightness) / 40,
				((int) lixiep->g * brightness) / 40, ((int) lixiep->b * brightness) / 40);
	}
		break;

	case COLORMODE_RBFULL:
	{
		int r, g, b;

		// calulate color for this loop
		hsl_to_rgb((int) (255.0 * rainbow_offset), 255, 128, &r, &g, &b);

		// generate color-structure for wished display-color
		actcolor = CRGB((r * brightness) / 100, (g * brightness) / 100,
				(b * brightness) / 100);
	}
		break;

	case COLORMODE_RBFIX:
	{
		int r, g, b;

		// calulate color for this loop
		hsl_to_rgb((int) (255.0 * xfmod((dpos * rainbow_posstep), 1.0)), 255, 128, &r, &g, &b);

		// generate color-structure for wished display-color
		actcolor = CRGB((r * brightness) / 100, (g * brightness) / 100,
				(b * brightness) / 100);
	}
		break;

	case COLORMODE_RBSHIFT:
	{
		int r, g, b;

		// calulate color for this loop
		hsl_to_rgb((int) (255.0 * xfmod(dpos * rainbow_posstep + rainbow_offset, 1.0)), 255, 128, &r, &g, &b);

		// generate color-structure for wished display-color
		actcolor = CRGB((r * brightness) / 100, (g * brightness) / 100,
				(b * brightness) / 100);
	}
		break;
	}
}

void num2led(int *ledpos, int number)
{
	int i;
	unsigned long mask = 0b00000000000000000001;

	for (i = 0; i < NUM_DIGIT_LEDS; i++)
	{
		if (mask & ledmap[number])
		{
			leds_curr[*ledpos + i] = 1.0;
		}
		else
		{
			leds_curr[*ledpos + i] = 0.0;
		}
		mask <<= 1;
	}
	*ledpos += NUM_DIGIT_LEDS;
}

#if HAS_COLONS
void colon2led(int *ledpos, unsigned char even, unsigned char ampm)
{
	int i, cmode = lixiep->cmode;
	unsigned char mask = 0b00000001;

	if (!even)
		mask <<= 4;
	if (datemode)
	{
		cmode = CMOD_DATE;
	}
	else
	{
		if ((lixiep->cmode == CMOD_AMPM) && ampm)
			mask <<= 2;
	}
	for (i = 0; i < NUM_COLON_LEDS; i++)
	{
		if (mask & colonmap[cmode])
		{
			leds_curr[*ledpos + i] = 1.0;
		}
		else
		{
			leds_curr[*ledpos + i] = 0.0;
		}
		mask <<= 1;
	}
	*ledpos += NUM_COLON_LEDS;
}
#endif

void sweep(void)
{
	int i, digitpos = 0, corrpos;
	float corrfact;

	if (datemode)
		--datemode;
	for (i = 0; i < NUM_LEDS; i++)
	{
		if (leds_last[i] < leds_curr[i])
		{
			leds_last[i] += leds_step;
		}
		else if (leds_last[i] > leds_curr[i])
		{
			leds_last[i] -= leds_step;
		}
    if(leds_last[i] > 1.0)
      leds_last[i] = 1.0;
   
		if ((leds_last[i] < 0.0) || (leds_last[i] < leds_step))
			leds_last[i] = 0.0;
		digitpos = get_digit_pos(i);
#if ! HAS_COLONS
		corrpos = i % NUM_DIGIT_LEDS;
		corrfact = corr_digit[corrpos];
#else
		if ((digitpos == 2) || (digitpos == 5)) // colons
		{
			corrfact = corr_colon[i & 1];
		}
		else
		{
			corrpos = i;
			if (digitpos >= 2) // colon 1
				corrpos -= 2;
			if (digitpos >= 5) // colon 2
				corrpos -= 2;
			corrpos = corrpos % (NUM_DIGIT_LEDS / 2);
			corrfact = corr_digit[corrpos];
		}
#endif
		rainbow_offset = xfmod(rainbow_offset + rainbow_step, 1.0);
		update_rainbow(digitpos);
		leds[i].r = (int) ((float) actcolor.r * leds_last[i] * corrfact);
		leds[i].g = (int) ((float) actcolor.g * leds_last[i] * corrfact);
		leds[i].b = (int) ((float) actcolor.b * leds_last[i] * corrfact);
#if ! _FASTLED_
		strip.setPixelColor(i, leds[i].r, leds[i].g, leds[i].b);
#endif
	}
#if _FASTLED_
	FastLED.show();
#else
	strip.show();
#endif
}

// dynamic generation of the web-servers response
void page_out(void)
{
	if (strlen(outstr))
	{
		server.sendContent(outstr);
		*outstr = 0;
	}
	else
	{
		server.sendContent(
				"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\"><html><head><meta http-equiv=\"content-type\"content=\"text/html; charset=ISO-8859-1\"><title>ESP8266_Lixie_Server</title></head>\r\n<body><h1 style=\"text-align: center; width: 504px;\" align=\"left\"><span style=\"color: rgb(0, 0, 153); font-weight: bold;\">ESP8266 Lixie-Server</span></h1><h3 style=\"text-align: center; width: 504px;\" align=\"left\"><span style=\"color: rgb(0, 0, 0); font-weight: bold;\">");
		server.sendContent(tstr2);
		server.sendContent(
				"</span></h3><form method=\"get\"><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" height=\"144\" width=\"487\"><tbody><tr><td width=\"120\"><b><big> Hour mode <td width=\"50\"></td><td><input name=\"HMOD24\" type=\"checkbox\" mode=\"submit\"");
		if (lixiep->h24)
			server.sendContent(" checked");
		server.sendContent(
				"> 24h </td></tr><tr><td><b><big>Colormode</big></b></td><td width=\"50\"></td><td><select name=\"MODE\"><option value=\"0\"");
		if (lixiep->mode == COLORMODE_FIX)
			server.sendContent(" selected");
		server.sendContent(">Fix</option><option value=\"1\"");
		if (lixiep->mode == COLORMODE_RBFULL)
			server.sendContent(" selected");
		server.sendContent(">Rainbow unicolor</option><option value=\"2\"");
		if (lixiep->mode == COLORMODE_RBFIX)
			server.sendContent(" selected");
		server.sendContent(">Rainbow multicolor</option><option value=\"3\"");
		if (lixiep->mode == COLORMODE_RBSHIFT)
			server.sendContent(" selected");
		server.sendContent(">Rainbow sweeping</option></select></td></tr>");
#if HAS_COLONS
		server.sendContent(
				"<tr><td width=\"120\"><b><big>Colons mode</big></b></td><td width=\"50\"></td><td><select name=\"CMODE\"><option value=\"0\"");
		if (lixiep->cmode == CMOD_NONE)
			server.sendContent(" selected");
		server.sendContent(">no colons</option><option value=\"1\"");
		if (lixiep->cmode == CMOD_STEADY)
			server.sendContent(" selected");
		server.sendContent(">steady</option><option value=\"2\"");
		if (lixiep->cmode == CMOD_BLINK)
			server.sendContent(" selected");
		server.sendContent(">blink</option><option value=\"3\"");
		if (lixiep->cmode == CMOD_ALTERNATE)
			server.sendContent(" selected");
		server.sendContent(">alternating</option><option value=\"4\"");
		if (lixiep->cmode == CMOD_AMPM)
			server.sendContent(" selected");
		server.sendContent(">AM/PM</option></select></td></tr>");
#endif
		server.sendContent(
				"<tr><td width=\"120\"><b><big>Date mode</big></b></td><td width=\"50\"></td><td><select name=\"DMODE\"><option value=\"0\"");
		if (lixiep->dmode == DMOD_NONE)
			server.sendContent(" selected");
		server.sendContent(">inactive</option><option value=\"1\"");
		if (lixiep->dmode == DMOD_MINUTE)
			server.sendContent(" selected");
		server.sendContent(">every minute</option><option value=\"2\"");
		if (lixiep->dmode == DMOD_HOUR)
			server.sendContent(" selected");
		server.sendContent(">every hour</option><option value=\"3\"");
		server.sendContent(">now</option></select></td></tr>");
		server.sendContent(
				"<tr><td><b><big>Brightness</big></b></td><td></td><td><input maxlength=\"3\" size=\"3\" name=\"BRIGHT\" value=\"");
		server.sendContent(String(lixiep->brightness));
		server.sendContent(
				"\"> %</td></tr><tr><td><b><big>Nightmode</font></big></b><td></td></td><td>Brightness<br><input maxlength=\"3\" size=\"3\" name=\"NMBRIGHT\" value=\"");
		server.sendContent(String(lixiep->nm_brightness));
		server.sendContent(
				"\"> %</td><td>Start<br><input maxlength=\"3\" size=\"3\" name=\"NMSTART\" value=\"");
    server.sendContent(((lixiep->nm_start > 9)? "" : "0") + String(lixiep->nm_start));
    server.sendContent(
        "\"> h</td><td>End<br><input maxlength=\"3\" size=\"3\" name=\"NMEND\" value=\"");
    server.sendContent(((lixiep->nm_end > 9)? "" : "0") + String(lixiep->nm_end));
		server.sendContent(
				"\"> h</td></tr><tr><td> <br></td></tr><tr><td><b><big>Fading</font></big></b></td><td></td><td><input maxlength=\"3\" size=\"3\" name=\"FADE\" value=\"");
		server.sendContent(String(lixiep->fade));
		server.sendContent(
				"\"> %</td></tr><tr><td><b><big><font color=\"#cc0000\">Red</font></big></b></td><td></td><td><input maxlength=\"3\" size=\"3\" name=\"RED\" value=\"");
		server.sendContent(String(lixiep->r));
		server.sendContent(
				"\"> %</td></tr><tr><td><b><big><font color=\"#006600\">Green</font></big></b></td><td></td><td><input maxlength=\"3\" size=\"3\" name=\"GREEN\" value=\"");
		server.sendContent(String(lixiep->g));
		server.sendContent(
				"\"> %</td></tr><tr><td><b><big><font color=\"#000099\">Blue</font></big></b></td><td></td><td><input maxlength=\"3\" size=\"3\" name=\"BLUE\" value=\"");
		server.sendContent(String(lixiep->b));
		server.sendContent(
				"\"> %</td></tr></tbody></table><br><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" height=\"30\" width=\"99\"><tbody><tr><td><input name=\"SEND\" value=\"  Send  \" type=\"submit\"></td></tr></tbody></table></form><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" height=\"30\" width=\"99\"><tbody><tr><td><form method=\"get\"><input name=\"SAVE\" value=\"Save\" type=\"submit\"></form></td></tr></tbody></table></body></html>\r\n");
	}
}

// system initalization
void setup()
{
	unsigned int i;
	unsigned char *eptr;

	pinMode(BUILTIN_LED, OUTPUT);   // Initialize the BUILTIN_LED pin as an output
	digitalWrite(BUILTIN_LED, HIGH);
	Serial.begin(115200);

	wifiManager.setTimeout(30);

	// initialize FastLED, all LEDs off
	memset(leds, 0, sizeof(CRGB) * NUM_LEDS);
	memset(leds_last, 0, sizeof(float) * NUM_LEDS);
	memset(leds_curr, 0, sizeof(float) * NUM_LEDS);

#if _FASTLED_
	FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
	FastLED.show();
#else
	strip.begin();
	strip.show(); // Initialize all pixels to 'off'
	strip.setBrightness(255);
#endif
	// read stored parameters
	EEPROM.begin(EEPROM_SIZE);
	eptr = (unsigned char*) lixiep;
	for (i = 0; i < sizeof(lixie_processor_struct); i++)
		*(eptr++) = EEPROM.read(i);

	// EEPROM-parameters invalid, use default-values and store them
	if (lixiep->valid != 0xA5)
	{
		lixiep->h24 = 1;
		lixiep->mode = COLORMODE_FIX;
		lixiep->cmode = CMOD_BLINK;
		lixiep->fade = 50;
		lixiep->r = 100;
		lixiep->g = 8;
		lixiep->b = 0;
		lixiep->brightness = 100;
		lixiep->nm_start = 0;
		lixiep->nm_end = 0;
		lixiep->nm_brightness = 50;
		lixiep->valid = 0xA5;

		eptr = (unsigned char*) lixiep;
		for (i = 0; i < sizeof(lixie_processor_struct); i++)
			EEPROM.write(i, *(eptr++));
		EEPROM.commit();
	}
	leds_step = 1.0 / lixiep->fade;
	update_rainbow(0);

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified name
	//here  "LixieAP"
	//and goes into a blocking loop awaiting configuration
	if (!wifiManager.autoConnect("LixieAP"))
	{
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	// if you get here you have connected to the WiFi
	Serial.println("connected...yeey :)");

	// get time from internet
	timeClient.update();

	d_mode.attachClick(do_dmode);

	// parsing function for the commands of the web-server
	server.on("/", []()
	{
		int i;
		unsigned int j;
		long pval;

		if(server.args())
		{
			for(i = 0; i < server.args(); i++)
			{
				if(server.argName(i) == "SEND")
				{
					*outstr = 0;
					lixiep->h24 = 0;
				}
			}
			for(i = 0; i < server.args(); i++)
			{
				errno = 0;
				if(server.argName(i) == "SAVE")
				{
					unsigned char *eptr = (unsigned char*)lixiep;

					for(j = 0; j < sizeof(lixie_processor_struct); j++)
					EEPROM.write(j, *(eptr++));
					EEPROM.commit();
					if(!server.arg(i).length())
					sprintf(outstr + strlen(outstr), "OK");
				}
				else if(server.argName(i) == "HMOD24")
				{
					if(server.arg(i) == "on")
					lixiep->h24 = 1;
					else if(server.arg(i) == "off")
					lixiep->h24 = 0;
					else
					sprintf(outstr + strlen(outstr), "HMOD24=%s\r\n", (lixiep->h24)?"on":"off");
				}
				else if(server.argName(i) == "MODE")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->mode = pval;
						if(lixiep->mode > 3)
						lixiep->mode = 3;
					}
					else
					sprintf(outstr + strlen(outstr), "MODE=%d\r\n", lixiep->mode);
				}
				else if(server.argName(i) == "CMODE")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->cmode = pval;
						if(lixiep->cmode > 4)
						lixiep->cmode = 4;
					}
					else
					sprintf(outstr + strlen(outstr), "CMODE=%d\r\n", lixiep->mode);
				}
				else if(server.argName(i) == "DMODE")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						if(pval >= DMOD_NOW)
						datemode = DATE_TIME * 100;
						else
						lixiep->dmode = pval;
					}
					else
					sprintf(outstr + strlen(outstr), "DMODE=%d\r\n", lixiep->dmode);
				}
				else if(server.argName(i) == "BRIGHT")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->brightness = pval;
						if(lixiep->brightness > 100)
						lixiep->brightness = 100;
					}
					else
					sprintf(outstr + strlen(outstr), "BRIGHT=%d\r\n", lixiep->brightness);
				}
				else if(server.argName(i) == "NMSTART")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->nm_start = abs(pval);
						if(lixiep->nm_start > 23)
						lixiep->nm_start = 0;
					}
					else
					sprintf(outstr + strlen(outstr), "NMSTART=%d\r\n", lixiep->nm_start);
				}
				else if(server.argName(i) == "NMEND")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->nm_end = abs(pval);
						if(lixiep->nm_end > 23)
						lixiep->nm_end = 0;
					}
					else
					sprintf(outstr + strlen(outstr), "NMEND=%d\r\n", lixiep->nm_end);
				}
				else if(server.argName(i) == "NMBRIGHT")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->nm_brightness = pval;
						if(lixiep->nm_brightness > 100)
						lixiep->nm_brightness = 100;
					}
					else
					sprintf(outstr + strlen(outstr), "NMBRIGHT=%d\r\n", lixiep->nm_brightness);
				}
				else if(server.argName(i) == "FADE")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->fade = pval;
						if(lixiep->fade > 80)
						lixiep->fade = 80;
						if(lixiep->fade < 1)
						lixiep->fade = 1;
						leds_step = 1.0 / lixiep->fade;
					}
					else
					sprintf(outstr + strlen(outstr), "FADE=%d\r\n", lixiep->fade);
				}
				else if(server.argName(i) == "RED")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->r = pval;
						if(lixiep->r > 100)
						lixiep->r = 100;
					}
					else
					sprintf(outstr + strlen(outstr), "RED=%d\r\n", lixiep->r);
				}
				else if(server.argName(i) == "GREEN")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->g = pval;
						if(lixiep->g > 100)
						lixiep->g = 100;
					}
					else
					sprintf(outstr + strlen(outstr), "GREEN=%d\r\n", lixiep->g);
				}
				else if(server.argName(i) == "BLUE")
				{
					pval = strtol(server.arg(i).c_str(), NULL, 10);
					if(server.arg(i).length() && !errno)
					{
						lixiep->b = pval;
						if(lixiep->b > 100)
						lixiep->b = 100;
					}
					else
					sprintf(outstr + strlen(outstr), "BLUE=%d\r\n", lixiep->b);
				}
			}
			*tstr2 = 0;
			update_timeleds();
		}
		page_out();
	});
	// start web-server
	server.begin();
	Serial.println("Web server started!");
	sweeper.attach(0.010, sweep);
}

void update_timeleds(void)
{
	static int lsec = 61, min;
	int sec, sec1, sec10, min1, min10, hr, hr1, hr10, hr24, ledpos;
	time_t rawtime, loctime;

	rawtime = timeClient.getEpochTime() + 1;	// get NTP-time
	loctime = myTZ.toLocal(rawtime);			// calc local time
	sec = second(loctime);						// get second
	min = minute(loctime);
	hr = hr24 = hour(loctime);

	if (!synced || (!hr24 && !min && !sec))		// sync if midnight or not synced
		synced = timeClient.update();			// NTP-update
	if (!lixiep->h24 && (hr24 >= 12))
		hr24 -= 12;
	if (sec != lsec)
	{
		if(lixiep->nm_start || lixiep->nm_end)
		{
			if(lixiep->nm_start > lixiep->nm_end)
			{
				brightness = ((hr >= lixiep->nm_start) || (hr < lixiep->nm_end))?lixiep->nm_brightness : lixiep->brightness;
			}
			else
			{
				brightness = ((hr >= lixiep->nm_start) && (hr < lixiep->nm_end))?lixiep->nm_brightness : lixiep->brightness;
			}
		}
		if (!sec)
		{
			if (lixiep->dmode)
			{
				if ((lixiep->dmode == DMOD_MINUTE) || !min)
				{
					datemode = DATE_TIME * 100;
				}
			}
		}

		lsec = sec;

		if (datemode)
		{
			sec = year(loctime) - 2000;
			min = month(loctime);
			hr24 = day(loctime);
		}

		sec1 = sec % 10;

		if (synced)
		{
			sec10 = sec / 10;
			min1 = min % 10;
			min10 = min / 10;
			hr1 = hr24 % 10;
			hr10 = hr24 / 10;
			digitalWrite(BUILTIN_LED, HIGH);		// blink for sync
		}
		else
		{
			sec10 = sec1;
			min1 = sec1;
			min10 = sec1;
			hr1 = sec1;
			hr10 = sec1;
		}

		ledpos = 0;

		num2led(&ledpos, hr10);
		num2led(&ledpos, hr1);
#if HAS_COLONS
		colon2led(&ledpos, sec1 & 1, hour(loctime) >= 12);
#endif
		num2led(&ledpos, min10);
		num2led(&ledpos, min1);
#if HAS_COLONS
		colon2led(&ledpos, sec1 & 1, hour(loctime) >= 12);
#endif
		num2led(&ledpos, sec10);
		num2led(&ledpos, sec1);

	}
	delay(10);
	digitalWrite(BUILTIN_LED, LOW);
}

void loop()
{
	// do the button functions
	d_mode.tick();
	update_timeleds();
	server.handleClient();						// handle HTTP-requests
}
