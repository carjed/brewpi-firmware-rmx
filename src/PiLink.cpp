/* Copyright (C) 2019 Lee C. Bussy (@LBussy)

This file is part of LBussy's BrewPi Firmware Remix (BrewPi-Firmware-RMX).

BrewPi Firmware RMX is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

BrewPi Firmware RMX is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with BrewPi Firmware RMX. If not, see <https://www.gnu.org/licenses/>.

These scripts were originally a part of firmware, a part of
the BrewPi project. Legacy support (for the very popular Arduino
controller) seems to have been discontinued in favor of new hardware.

All credit for the original firmware goes to @elcojacobs,
@m-mcgowan, @elnicoCZ, @ntfreak, @Gargy007 and I'm sure many more
contributors around the world. My apologies if I have missed anyone;
those were the names listed as contributors on the Legacy branch.

See: 'original-license.md' for notes about the original project's
license and credits. */

#include "Brewpi.h"
#include <stdarg.h>

#include "stddef.h"
#include "PiLink.h"

#include "Version.h"
#include "TempControl.h"
#include "Display.h"
#include "JsonKeys.h"
#include "Ticks.h"
#include "Brewpi.h"
#include "EepromManager.h"
#include "EepromFormat.h"
#include "SettingsManager.h"
#include "Display.h"
#include "PiLinkHandlers.h"
#include "UI.h"
#include "Actuator.h"
#include "DHT.h"
#include "HumiditySensor.h"
#include "FanControl.h"

#if BREWPI_SIMULATE
#include "Simulator.h"
#endif

static const char STR_WEB_INTERFACE[] PROGMEM = "by web";
static const char STR_TEMPERATURE_PROFILE[] PROGMEM = "by profile";
static const char STR_MODE[] PROGMEM = "Mode";
static const char STR_BEER_TEMP[] PROGMEM = "Beer";
static const char STR_FRIDGE_TEMP[] PROGMEM = "Fridge";
static const char STR_FMT_SET_TO[] PROGMEM = PRINTF_PROGMEM " set to %s " PRINTF_PROGMEM;

// Rename Serial to piStream, to abstract it for later platform independence

#if BREWPI_EMULATE
class MockSerial : public Stream
{
public:
	void print(char c) {}
	void print(const char *c) {}
	void printNewLine() {}
	void println() {}
	int read() { return -1; }
	int available() { return -1; }
	void begin(unsigned long) {}
	size_t write(uint8_t w) { return 1; }
	int peek() { return -1; }
	void flush(){};
	operator bool() { return true; }
};

static MockSerial mockSerial;
#define piStream mockSerial
#elif !defined(WIRING)
StdIO stdIO;
#define piStream stdIO
#else
#define piStream Serial
#ifdef SPARK
#define SERIAL_READY(x) 1
#else
#define SERIAL_READY(x) x
#endif
#endif

bool PiLink::firstPair;
char PiLink::printfBuff[PRINTF_BUFFER_SIZE];

void PiLink::init(void)
{
	piStream.begin(BAUD);
}

// Create a printf like interface to the Arduino Serial function. Format
// string stored in PROGMEM
void PiLink::print_P(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf_P(printfBuff, PRINTF_BUFFER_SIZE, fmt, args);
	va_end(args);
	if (SERIAL_READY(piStream))
	{ // if Serial connected (on Leonardo)
		piStream.print(printfBuff);
	}
}

// Create a printf like interface to the Arduino Serial function. Format
// string stored in RAM
void PiLink::print(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(printfBuff, PRINTF_BUFFER_SIZE, fmt, args);
	va_end(args);
	if (SERIAL_READY(piStream))
	{
		piStream.print(printfBuff);
	}
}

void PiLink::printNewLine()
{
	piStream.println();
}

void printNibble(uint8_t n)
{
	n &= 0xF;
	piStream.print((char)(n >= 10 ? n - 10 + 'A' : n + '0'));
}

void PiLink::receive(void)
{
	while (piStream.available() > 0)
	{
		char inByte = piStream.read();
		switch (inByte)
		{
		case ' ':
		case '\n':
		case '\r':
			break;

#if BREWPI_SIMULATE == 1
		case 'y':
			parseJson(HandleSimulatorConfig);
			break;
		case 'Y':
			printSimulatorSettings();
			break;
#endif
		case 'A': // alarm on
			soundAlarm(true);
			break;
		case 'a': // alarm off
			soundAlarm(false);
			break;

		case 't': // Temperatures requested
			printTemperatures();
			break;
		case 'C': // Set default constants
			tempControl.loadDefaultConstants();
			// Reprint stationary text to update to correct degree unit
			display.printStationaryText();
			// Update script with new settings
			sendControlConstants();
			logInfo(INFO_DEFAULT_CONSTANTS_LOADED);
			break;
		case 'S': // Set default settings
			tempControl.loadDefaultSettings();
			// Update script with new settings
			sendControlSettings();
			logInfo(INFO_DEFAULT_SETTINGS_LOADED);
			break;
		case 's': // Control settings requested
			sendControlSettings();
			break;
		case 'c': // Control constants requested
			sendControlConstants();
			break;
		case 'v': // Control variables requested
			sendControlVariables();
			break;
		case 'n':
			// v version
			// s shield type
			// y: simulator
			// b: board
			print_P(PSTR("N:{"
						 "\"v\":\"" PRINTF_PROGMEM "\","
						 "\"n\":\"" PRINTF_PROGMEM "\","
						 "\"s\":%d,"
						 "\"y\":%d,"
						 "\"b\":\"%c\","
						 "\"l\":\"%d\""
						 "}"),
					PSTR(stringify(VERSION_STRING)),// v:
					PSTR(stringify(BUILD_NAME)),	// n:
					BREWPI_STATIC_CONFIG,			// s:
					BREWPI_SIMULATE,				// y:
					BREWPI_BOARD,					// b:
					BREWPI_LOG_MESSAGES_VERSION);	// l:
			printNewLine();
			break;
		case 'l': // Display content requested
			printResponse('L');
			piStream.print('[');
			char stringBuffer[21];
			for (uint8_t i = 0; i < 4; i++)
			{
				display.getLine(i, stringBuffer);
				print_P(PSTR("\"%s\""), stringBuffer);
				char close = (i < 3) ? ',' : ']';
				piStream.print(close);
			}
			printNewLine();
			break;
		case 'j': // Receive settings as json
			receiveJson();
			break;

#if BREWPI_EEPROM_HELPER_COMMANDS
		case 'e': // Dump contents of eeprom
			openListResponse('E');
			for (uint16_t i = 0; i < 1024;)
			{
				if (i > 0)
				{
					piLink.printNewLine();
					piLink.print(',');
				}
				piLink.print('\"');
				for (uint8_t j = 0; j < 64; j++)
				{
					uint8_t d = eepromAccess.readByte(i++);
					printNibble(d >> 4);
					printNibble(d);
				}
				piLink.print('\"');
			}
			closeListResponse();
			break;
#endif

		case 'E': // Initialize eeprom
			eepromManager.initializeEeprom();
			logInfo(INFO_EEPROM_INITIALIZED);
			settingsManager.loadSettings();
			break;

		case 'd': // List devices in eeprom order
			openListResponse('d');
			deviceManager.listDevices(piStream);
			closeListResponse();
			break;

		case 'U': // Update device
			deviceManager.parseDeviceDefinition(piStream);
			break;

		case 'h': // Hardware query
			openListResponse('h');
			deviceManager.enumerateHardwareToStream(piStream);
			closeListResponse();
			break;

#if (BREWPI_DEBUG > 0)
		case 'Z': // Zap eeprom
			eepromManager.zapEeprom();
			logInfo(INFO_EEPROM_ZAPPED);
			break;
#endif

		case 'R': // reset
			handleReset();
			break;

		// case 'F': // Flash firmware
		// 	flashFirmware();
		// 	break;

		default:
			logWarningInt(WARNING_INVALID_COMMAND, inByte);
		}
	}
}

#define COMPACT_SERIAL BREWPI_SIMULATE
#if COMPACT_SERIAL
#define JSON_BEER_TEMP "bt"
#define JSON_BEER_SET "bs"
#define JSON_BEER_ANN "ba"
#define JSON_FRIDGE_TEMP "ft"
#define JSON_FRIDGE_SET "fs"
#define JSON_FRIDGE_ANN "fa"
#define JSON_FRIDGE_HUMIDITY "fh"
#define JSON_STATE "s"
#define JSON_TIME "t"
#define JSON_ROOM_TEMP "rt"

temperature beerTemp = -1, beerSet = -1, fridgeTemp = -1, fridgeSet = -1, fridgeHumidity = -1;
double roomTemp = -1;
uint8_t state = 0xFF;
char *beerAnn;
char *fridgeAnn;

typedef char *PChar;
inline bool changed(uint8_t &a, uint8_t b)
{
	uint8_t c = a;
	a = b;
	return b != c;
}
inline bool changed(temperature &a, temperature b)
{
	temperature c = a;
	a = b;
	return b != c;
}
inline bool changed(double &a, double b)
{
	double c = a;
	a = b;
	return b != c;
}
inline bool changed(PChar &a, PChar b)
{
	PChar c = a;
	a = b;
	return b != c;
}
#else
#define JSON_BEER_TEMP "BeerTemp"
#define JSON_BEER_SET "BeerSet"
#define JSON_BEER_ANN "BeerAnn"
#define JSON_FRIDGE_TEMP "FridgeTemp"
#define JSON_FRIDGE_HUMIDITY "FridgeHumidity"
#define JSON_FRIDGE_SET "FridgeSet"
#define JSON_FRIDGE_ANN "FridgeAnn"
#define JSON_STATE "State"
#define JSON_TIME "Time"
#define JSON_ROOM_TEMP "RoomTemp"

#define changed(a, b) 1
#endif

void PiLink::printTemperaturesJSON(char *beerAnnotation, char *fridgeAnnotation)
{
	printResponse('T');

	temperature t;
	t = tempControl.getBeerTemp();
	if (changed(beerTemp, t))
		sendJsonTemp(PSTR(JSON_BEER_TEMP), t);

	t = tempControl.getBeerSetting();
	if (changed(beerSet, t))
		sendJsonTemp(PSTR(JSON_BEER_SET), t);

	if (changed(beerAnn, beerAnnotation))
		sendJsonAnnotation(PSTR(JSON_BEER_ANN), beerAnnotation);

	t = tempControl.getFridgeTemp();
	if (changed(fridgeTemp, t))
		sendJsonTemp(PSTR(JSON_FRIDGE_TEMP), t);

	t = tempControl.getFridgeSetting();
	if (changed(fridgeSet, t))
		sendJsonTemp(PSTR(JSON_FRIDGE_SET), t);

	if (changed(fridgeAnn, fridgeAnnotation))
		sendJsonAnnotation(PSTR(JSON_FRIDGE_ANN), fridgeAnnotation);

	t = tempControl.getRoomTemp();
	if (tempControl.ambientSensor->isConnected() && changed(roomTemp, t))
		sendJsonTemp(PSTR(JSON_ROOM_TEMP), tempControl.getRoomTemp());

	if (changed(state, tempControl.getState()))
		sendJsonPair(PSTR(JSON_STATE), (uint8_t)tempControl.getState());

	humidity h;
	h = tempControl.getFridgeHumidity();
	if (changed(fridgeHumidity, h))
		sendJsonTemp(PSTR(JSON_FRIDGE_HUMIDITY), h);


#if BREWPI_SIMULATE
	printJsonName(PSTR(JSON_TIME));
	print_P(PSTR("%lu"), ticks.millis() / 1000);
#endif
	sendJsonClose();
}

void PiLink::sendJsonAnnotation(const char *name, const char *annotation)
{
	printJsonName(name);
	const char *fmtAnn = annotation ? PSTR("\"%s\"") : PSTR("null");
	print_P(fmtAnn, annotation);
}

void PiLink::sendJsonTemp(const char *name, temperature temp)
{
	char tempString[9];
	tempToString(tempString, temp, 2, 9);
	printJsonName(name);
	piStream.print(tempString);
}

void PiLink::sendJsonHumidity(const char *name, humidity hum)
{
	char tempString[9];
	tempToString(tempString, hum, 2, 9);
	printJsonName(name);
	piStream.print(tempString);
}

void PiLink::printTemperatures(void)
{
	// Print all temperatures with empty annotations
	printTemperaturesJSON(0, 0);
}

void PiLink::printBeerAnnotation(const char *annotation, ...)
{
	// Using print_P for the Annotation fails. Arguments are not passed
	// correctly. Use Serial directly as a work around.
	char tempString[32]; // Resulting string limited to 32 chars
	va_list args;
	va_start(args, annotation);
	vsnprintf_P(tempString, 32, annotation, args);
	va_end(args);
	printTemperaturesJSON(tempString, 0);
}

void PiLink::printFridgeAnnotation(const char *annotation, ...)
{
	// Using print_P for the Annotation fails. Arguments are not passed
	// correctly. Use Serial directly as a work around.
	char tempString[32]; // Resulting string limited to 32 chars
	va_list args;
	va_start(args, annotation);
	vsnprintf_P(tempString, 32, annotation, args);
	va_end(args);
	printTemperaturesJSON(0, tempString);
}

void PiLink::printResponse(char type)
{
	piStream.print(type);
	piStream.print(':');
	firstPair = true;
}

void PiLink::openListResponse(char type)
{
	printResponse(type);
	piStream.print('[');
}

void PiLink::closeListResponse()
{
	piStream.print(']');
	printNewLine();
}

#if BREWPI_DEBUG == 0
void PiLink::debugMessage(const char *message, ...){}
#else
void PiLink::debugMessage(const char *message, ...)
{
	// Using print_P for the Annotation fails. Arguments are not passed
	// correctly. Use Serial directly as a work around.
	va_list args;

	// Print 'D:' as prefix
	printResponse('D');

	va_start(args, message);
	vsnprintf_P(printfBuff, PRINTF_BUFFER_SIZE, message, args);
	va_end(args);
	piStream.print(printfBuff);
	printNewLine();
}
#endif

void PiLink::sendJsonClose()
{
	piStream.print('}');
	printNewLine();
}

// Send settings as JSON string
void PiLink::sendControlSettings(void)
{
	char tempString[12];
	printResponse('S');
	ControlSettings &cs = tempControl.cs;
	FanControlSettings &fcs = fanControl.cs;
	sendJsonPair(JSONKEY_mode, cs.mode);
	sendJsonPair(JSONKEY_beerSetting, tempToString(tempString, cs.beerSetting, 2, 12));
	sendJsonPair(JSONKEY_fridgeSetting, tempToString(tempString, cs.fridgeSetting, 2, 12));
	sendJsonPair(JSONKEY_fanDuty, tempToString(tempString, fcs.fanSetting, 2, 12));
	sendJsonPair(JSONKEY_heatEstimator, fixedPointToString(tempString, cs.heatEstimator, 3, 12));
	sendJsonPair(JSONKEY_coolEstimator, fixedPointToString(tempString, cs.coolEstimator, 3, 12));
	sendJsonClose();
}

// Location to which the offset is relative. This saves having to store a
// full 16-bit pointer. Because the structs are static, we can only compute
// an offset relative to the struct (cc,cs,cv etc..) rather than offset from
// tempControl.
uint8_t *jsonOutputBase;

void PiLink::jsonOutputUint8(const char *key, uint8_t offset)
{
	piLink.sendJsonPair(key, *(jsonOutputBase + offset));
}

void PiLink::jsonOutputUint16(const char *key, uint8_t offset)
{
	piLink.sendJsonPair(key, *((uint16_t *)(jsonOutputBase + offset)));
}

/**
 * Outputs the temperature at the given offset from tempControl.cc.
 * The temperature is assumed to be an internal fixed point value.
 */
void PiLink::jsonOutputTempToString(const char *key, uint8_t offset)
{
	char buf[12];
	piLink.sendJsonPair(key, tempToString(buf, *((temperature *)(jsonOutputBase + offset)), 1, 12));
}

void PiLink::jsonOutputFixedPointToString(const char *key, uint8_t offset)
{
	char buf[12];
	piLink.sendJsonPair(key, fixedPointToString(buf, *((temperature *)(jsonOutputBase + offset)), 3, 12));
}

void PiLink::jsonOutputTempDiffToString(const char *key, uint8_t offset)
{
	char buf[12];
	piLink.sendJsonPair(key, tempDiffToString(buf, *((temperature *)(jsonOutputBase + offset)), 3, 12));
}

void PiLink::jsonOutputChar(const char *key, uint8_t offset)
{
	piLink.sendJsonPair(key, *((char *)(jsonOutputBase + offset)));
}

typedef void (*JsonOutputCCHandler)(const char *key, uint8_t offset);

enum JsonOutputIndex
{
	JOCC_UINT8 = 0,
	JOCC_TEMP_FORMAT = 1,
	JOCC_FIXED_POINT = 2,
	JOCC_TEMP_DIFF = 3,
	JOCC_CHAR = 4,
	JOCC_UINT16 = 5,
};

const PiLink::JsonOutputHandler PiLink::JsonOutputHandlers[] = {
	PiLink::jsonOutputUint8,
	PiLink::jsonOutputTempToString,
	PiLink::jsonOutputFixedPointToString,
	PiLink::jsonOutputTempDiffToString,
	PiLink::jsonOutputChar,
	PiLink::jsonOutputUint16,
};

#define JSON_OUTPUT_CC_MAP(name, fn)                         \
	{                                                        \
		JSONKEY_##name, offsetof(ControlConstants, name), fn \
	}
#define JSON_OUTPUT_CV_MAP(name, fn)                         \
	{                                                        \
		JSONKEY_##name, offsetof(ControlVariables, name), fn \
	}
#define JSON_OUTPUT_CS_MAP(name, fn)                        \
	{                                                       \
		JSONKEY_##name, offsetof(ControlSettings, name), fn \
	}

const PiLink::JsonOutput PiLink::jsonOutputCCMap[] PROGMEM = {
	JSON_OUTPUT_CC_MAP(tempFormat, JOCC_CHAR),
	JSON_OUTPUT_CC_MAP(tempSettingMin, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CC_MAP(tempSettingMax, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CC_MAP(pidMax, JOCC_TEMP_DIFF),

	JSON_OUTPUT_CC_MAP(Kp, JOCC_FIXED_POINT),
	JSON_OUTPUT_CC_MAP(Ki, JOCC_FIXED_POINT),
	JSON_OUTPUT_CC_MAP(Kd, JOCC_FIXED_POINT),

	JSON_OUTPUT_CC_MAP(iMaxError, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(idleRangeHigh, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(idleRangeLow, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(heatingTargetUpper, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(heatingTargetLower, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(coolingTargetUpper, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(coolingTargetLower, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CC_MAP(maxHeatTimeForEstimate, JOCC_UINT16),
	JSON_OUTPUT_CC_MAP(maxCoolTimeForEstimate, JOCC_UINT16),

	JSON_OUTPUT_CC_MAP(fridgeFastFilter, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(fridgeSlowFilter, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(fridgeSlopeFilter, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(beerFastFilter, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(beerSlowFilter, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(beerSlopeFilter, JOCC_UINT8),

	JSON_OUTPUT_CC_MAP(lightAsHeater, JOCC_UINT8),
	JSON_OUTPUT_CC_MAP(rotaryHalfSteps, JOCC_UINT8)};

void PiLink::sendJsonValues(char responseType, const JsonOutput * /*PROGMEM*/ jsonOutputMap, uint8_t mapCount)
{
	printResponse(responseType);
	while (mapCount-- > 0)
	{
		JsonOutput output;
		memcpy_P(&output, jsonOutputMap++, sizeof(output));
		JsonOutputHandlers[output.handlerOffset](output.key, output.offset);
	}
	sendJsonClose();
}

// Send control constants as JSON string. Might contain spaces between minus
// sign and number. Python will have to strip these.
void PiLink::sendControlConstants(void)
{
	jsonOutputBase = (uint8_t *)&tempControl.cc;
	sendJsonValues('C', jsonOutputCCMap, sizeof(jsonOutputCCMap) / sizeof(jsonOutputCCMap[0]));
}

const PiLink::JsonOutput PiLink::jsonOutputCVMap[] PROGMEM = {
	JSON_OUTPUT_CV_MAP(beerDiff, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CV_MAP(diffIntegral, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CV_MAP(beerSlope, JOCC_TEMP_DIFF),
	JSON_OUTPUT_CV_MAP(p, JOCC_FIXED_POINT),
	JSON_OUTPUT_CV_MAP(i, JOCC_FIXED_POINT),
	JSON_OUTPUT_CV_MAP(d, JOCC_FIXED_POINT),
	JSON_OUTPUT_CV_MAP(estimatedPeak, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CV_MAP(negPeakEstimate, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CV_MAP(posPeakEstimate, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CV_MAP(negPeak, JOCC_TEMP_FORMAT),
	JSON_OUTPUT_CV_MAP(posPeak, JOCC_TEMP_FORMAT)};

// Send all control variables. Useful for debugging and choosing parameters
void PiLink::sendControlVariables(void)
{
	jsonOutputBase = (uint8_t *)&tempControl.cv;
	sendJsonValues('V', jsonOutputCVMap, sizeof(jsonOutputCVMap) / sizeof(jsonOutputCVMap[0]));
}

void PiLink::printJsonName(const char *name)
{
	printJsonSeparator();
	piStream.print('"');
	print_P(name);
	piStream.print('"');
	piStream.print(':');
}

inline void PiLink::printJsonSeparator()
{
	piStream.print(firstPair ? '{' : ',');
	firstPair = false;
}

void PiLink::sendJsonPair(const char *name, const char *val)
{
	printJsonName(name);
	piStream.print(val);
}

void PiLink::sendJsonPair(const char *name, char val)
{
	printJsonName(name);
	piStream.print('"');
	piStream.print(val);
	piStream.print('"');
}

void PiLink::sendJsonPair(const char *name, uint16_t val)
{
	printJsonName(name);
	print_P(PSTR("%u"), val);
}

void PiLink::sendJsonPair(const char *name, uint8_t val)
{
	sendJsonPair(name, (uint16_t)val);
}

int readNext()
{
	uint16_t retries = 0;
	while (piStream.available() == 0)
	{
		wait.microseconds(100);
		retries++;
		if (retries >= 10000)
		{
			return -1;
		}
	}
	return piStream.read();
}

bool parseJsonToken(char *val)
{  // Parses a token from the piStream, return true if a token was parsed
	uint8_t index = 0;
	val[0] = 0;
	bool result = true;
	for (;;) // get value
	{
		int character = readNext();
		if (index == 29 || character == '}' || character == -1)
		{
			result = false;
			break;
		}
		if (character == ',' || character == ':') // End of value
			break;
		if (character == ' ' || character == '"')
		{
			; // Skip spaces and apostrophes
		}
		else
			val[index++] = character;
	}
	val[index] = 0; // Null terminate string
	return result;
}

void PiLink::parseJson(ParseJsonCallback fn, void *data)
{
	char key[30];
	char val[30];
	*key = 0;
	*val = 0;
	bool next = true;
	// Read first open brace
	int c = readNext();
	if (c != '{')
	{
		logErrorInt(ERROR_EXPECTED_BRACKET, c);
		return;
	}
	do
	{
		next = parseJsonToken(key) && parseJsonToken(val);
		if (val[0] && key[0])
			fn(key, val, data);
	} while (next);
}

void PiLink::receiveJson(void)
{
	parseJson(&processJsonPair, NULL);
	
#if !BREWPI_SIMULATE
	// This is a lot of overhead and not needed for the simulator	   
	sendControlSettings(); // Update script with new settings
	sendControlConstants();
#endif
	return;
}

void PiLink::setMode(const char *val)
{
	char mode = val[0];
	tempControl.setMode(mode);
	piLink.printFridgeAnnotation(STR_FMT_SET_TO, STR_MODE, val, STR_WEB_INTERFACE);
}

void PiLink::setBeerSetting(const char *val)
{
	const char *source = NULL;
	temperature newTemp;
	if (!stringToTemp(&newTemp, val))
	{
		return; // could not parse value
	}
	if (tempControl.cs.mode == 'p')
	{
		if (abs(newTemp - tempControl.cs.beerSetting) > 100)
		{ // This excludes gradual updates under 0.2 degrees
			source = STR_TEMPERATURE_PROFILE;
		}
	}
	else
	{
		source = STR_WEB_INTERFACE;
	}
	if (source)
	{
		printBeerAnnotation(STR_FMT_SET_TO, STR_BEER_TEMP, val, source);
	}
	tempControl.setBeerTemp(newTemp);
}

void PiLink::setFridgeSetting(const char *val)
{
	temperature newTemp;
	if (!stringToTemp(&newTemp, val))
	{
		return; // Could not parse value
	}
	if (tempControl.cs.mode == 'f')
	{
		printFridgeAnnotation(STR_FMT_SET_TO, STR_FRIDGE_TEMP, val, STR_WEB_INTERFACE);
	}
	tempControl.setFridgeTemp(newTemp);
}

void PiLink::setFanDuty(const char *val)
{
	fan_level newFanLevel;
	if (!stringToTemp(&newFanLevel, val))
	{
		return; // Could not parse value
	}

	fanControl.setDuty(newFanLevel);
}


void PiLink::setTempFormat(const char *val)
{
	tempControl.cc.tempFormat = val[0];
	// Reprint stationary text to update to correct degree unit
	display.printStationaryText();
	eepromManager.storeTempConstantsAndSettings();
}

// TODO - Move these structs to PROGMEM.
enum FilterType
{
	FAST,
	SLOW,
	SLOPE
};
enum TempSensorTarget
{
	FRIDGE,
	BEER
};

static uint8_t *const filterSettings[] = {
	&tempControl.cc.fridgeFastFilter,
	&tempControl.cc.fridgeSlowFilter,
	&tempControl.cc.fridgeSlopeFilter,
	&tempControl.cc.beerFastFilter,
	&tempControl.cc.beerSlowFilter,
	&tempControl.cc.beerSlopeFilter};

#define MAKE_FILTER_SETTING_TARGET(filterType, sensorTarget) (void *)(uint8_t(filterType) + uint8_t(sensorTarget) * 3)

void applyFilterSetting(const char *val, void *target)
{
// The cast was (uint8_t(uint16_t(target), changed to unsigned int so that
// the first cast is the same width as a pointer, avoiding a warning.
// On x64 builds, unsigned int is still 32 bits, so cast to uint64_t instead
#if defined(_M_X64) || defined(__amd64__)
	uint8_t offset = uint8_t((uint64_t)target); // target is really just an integer
#else
	uint8_t offset = uint8_t((unsigned int)target); // target is really just an integer
#endif

	FilterType filterType = FilterType(offset & 3);
	TempSensorTarget sensorTarget = TempSensorTarget(offset / 3);

	uint8_t value = atol(val);
	uint8_t *const location = filterSettings[offset];
	*location = value;
	TempSensor *sensor = sensorTarget ? tempControl.beerSensor : tempControl.fridgeSensor;
	switch (filterType)
	{
	case FAST:
		sensor->setFastFilterCoefficients(value);
		break;
	case SLOW:
		sensor->setSlowFilterCoefficients(value);
		break;
	case SLOPE:
		sensor->setSlopeFilterCoefficients(value);
		break;
	}
	eepromManager.storeTempConstantsAndSettings();
}

void setStringToFixedPoint(const char *value, temperature *target)
{
	if (stringToFixedPoint(target, value))
	{	// Value parsed correctly
		eepromManager.storeTempConstantsAndSettings();
	}
}

void setStringToTemp(const char *value, temperature *target)
{
	if (stringToTemp(target, value))
	{	// Value parsed correctly
		eepromManager.storeTempConstantsAndSettings();
	}
}

void setStringToTempDiff(const char *value, temperature *target)
{
	if (stringToTempDiff(target, value))
	{	// Value parsed correctly
		eepromManager.storeTempConstantsAndSettings();
	}
}

void setUint16(const char *value, uint16_t *target)
{
	if (stringToUint16(target, value))
	{	// Value parsed correctly
		eepromManager.storeTempConstantsAndSettings();
	}
}

void setBool(const char *value, uint8_t *target)
{
	bool result;
	if (stringToBool(&result, value))
	{	// Convert bool to uint8_t
		*target = result;
		eepromManager.storeTempConstantsAndSettings();
	}
}

#define JSON_CONVERT(jsonKey, target, fn)         \
	{                                             \
		jsonKey, target, (JsonParserHandlerFn)&fn \
	}

const PiLink::JsonParserConvert PiLink::jsonParserConverters[] PROGMEM = {
	JSON_CONVERT(JSONKEY_mode, NULL, setMode),
	JSON_CONVERT(JSONKEY_beerSetting, NULL, setBeerSetting),
	JSON_CONVERT(JSONKEY_fridgeSetting, NULL, setFridgeSetting),
	JSON_CONVERT(JSONKEY_fanDuty, NULL, setFanDuty),

	JSON_CONVERT(JSONKEY_heatEstimator, &tempControl.cs.heatEstimator, setStringToFixedPoint),
	JSON_CONVERT(JSONKEY_coolEstimator, &tempControl.cs.coolEstimator, setStringToFixedPoint),

	JSON_CONVERT(JSONKEY_tempFormat, NULL, setTempFormat),

	JSON_CONVERT(JSONKEY_tempSettingMin, &tempControl.cc.tempSettingMin, setStringToTemp),
	JSON_CONVERT(JSONKEY_tempSettingMax, &tempControl.cc.tempSettingMax, setStringToTemp),
	JSON_CONVERT(JSONKEY_pidMax, &tempControl.cc.pidMax, setStringToTempDiff),

	JSON_CONVERT(JSONKEY_Kp, &tempControl.cc.Kp, setStringToFixedPoint),
	JSON_CONVERT(JSONKEY_Ki, &tempControl.cc.Ki, setStringToFixedPoint),
	JSON_CONVERT(JSONKEY_Kd, &tempControl.cc.Kd, setStringToFixedPoint),

	JSON_CONVERT(JSONKEY_iMaxError, &tempControl.cc.iMaxError, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_idleRangeHigh, &tempControl.cc.idleRangeHigh, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_idleRangeLow, &tempControl.cc.idleRangeLow, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_heatingTargetUpper, &tempControl.cc.heatingTargetUpper, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_heatingTargetLower, &tempControl.cc.heatingTargetLower, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_coolingTargetUpper, &tempControl.cc.coolingTargetUpper, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_coolingTargetLower, &tempControl.cc.coolingTargetLower, setStringToTempDiff),
	JSON_CONVERT(JSONKEY_maxHeatTimeForEstimate, &tempControl.cc.maxHeatTimeForEstimate, setUint16),
	JSON_CONVERT(JSONKEY_maxCoolTimeForEstimate, &tempControl.cc.maxCoolTimeForEstimate, setUint16),
	JSON_CONVERT(JSONKEY_lightAsHeater, &tempControl.cc.lightAsHeater, setBool),
	JSON_CONVERT(JSONKEY_rotaryHalfSteps, &tempControl.cc.rotaryHalfSteps, setBool),

	JSON_CONVERT(JSONKEY_fridgeFastFilter, MAKE_FILTER_SETTING_TARGET(FAST, FRIDGE), applyFilterSetting),
	JSON_CONVERT(JSONKEY_fridgeSlowFilter, MAKE_FILTER_SETTING_TARGET(SLOW, FRIDGE), applyFilterSetting),
	JSON_CONVERT(JSONKEY_fridgeSlopeFilter, MAKE_FILTER_SETTING_TARGET(SLOPE, FRIDGE), applyFilterSetting),
	JSON_CONVERT(JSONKEY_beerFastFilter, MAKE_FILTER_SETTING_TARGET(FAST, BEER), applyFilterSetting),
	JSON_CONVERT(JSONKEY_beerSlowFilter, MAKE_FILTER_SETTING_TARGET(SLOW, BEER), applyFilterSetting),
	JSON_CONVERT(JSONKEY_beerSlopeFilter, MAKE_FILTER_SETTING_TARGET(SLOPE, BEER), applyFilterSetting)};

void PiLink::processJsonPair(const char *key, const char *val, void *pv)
{
	logInfoStringString(INFO_RECEIVED_SETTING, key, val);

	for (uint8_t i = 0; i < sizeof(jsonParserConverters) / sizeof(jsonParserConverters[0]); i++)
	{
		JsonParserConvert converter;
		memcpy_P(&converter, &jsonParserConverters[i], sizeof(converter));
		if (strcmp_P(key, converter.key) == 0)
		{
			converter.fn(val, converter.target);
			return;
		}
	}
	logWarning(WARNING_COULD_NOT_PROCESS_SETTING);
}

extern ValueActuator alarm;
void PiLink::soundAlarm(bool active)
{
	alarm.setActive(active);
}

#ifndef ARDUINO
void PiLink::print(char c)
{
	piStream.print(c);
}
#endif
