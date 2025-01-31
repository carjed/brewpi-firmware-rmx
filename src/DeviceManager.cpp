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
#include "BrewpiStrings.h"
#include "DeviceManager.h"
#include "TempControl.h"
#include "FanControl.h"
#include "HumiditySensor.h"
#include "Actuator.h"
#include "Sensor.h"
#include "TempSensorDisconnected.h"
#include "TempSensorExternal.h"
#include "PiLink.h"
#include "EepromFormat.h"
#include "EepromManager.h"

#define CALIBRATION_OFFSET_PRECISION (4)

#ifdef WIRING
#include "OneWireTempSensor.h"
//#include "OneWireActuator.h"
//#include "DS2413.h"
// #include "DHT.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include "ActuatorPin.h"
#include "SensorPin.h"
#endif

class OneWire;

/*
 * Defaults for sensors, actuators and temperature sensors when not defined in the eeprom.
 */
ValueSensor<bool> defaultSensor(false); // off
DisconnectedHumiditySensor defaultHumiditySensor;
ValueActuator defaultActuator;
DisconnectedTempSensor defaultTempSensor;

bool DeviceManager::firstDeviceOutput;

bool DeviceManager::isDefaultTempSensor(BasicTempSensor *sensor)
{
	return sensor == &defaultTempSensor;
}

/**
 * Sets devices to their unconfigured states. Each device is initialized to a static no-op instance.
 * This method is idempotent, and is called each time the eeprom is reset. 
 */
void DeviceManager::setupUnconfiguredDevices()
{
	// right now, uninstall doesn't care about chamber/beer distinction.
	// but this will need to match beer/function when multiferment is available
	DeviceConfig cfg;
	cfg.chamber = 1;
	cfg.beer = 1;
	for (uint8_t i = 0; i < DEVICE_MAX; i++)
	{
		cfg.deviceFunction = DeviceFunction(i);
		uninstallDevice(cfg);
	}
}

/**
 * Creates a new device for the given config.
 */
void *DeviceManager::createDevice(DeviceConfig &config, DeviceType dt)
{
	switch (config.deviceHardware)
	{
	case DEVICE_HARDWARE_NONE:
		break;
	case DEVICE_HARDWARE_PIN:
		if (dt == DEVICETYPE_SWITCH_SENSOR)
#if BREWPI_SIMULATE
			return new ValueSensor<bool>(false);
#else
			return new DigitalPinSensor(config.hw.pinNr, config.hw.invert);
#endif
		else if (dt == DEVICETYPE_HUM_SENSOR)

			return new HumiditySensor(config.hw.pinNr);

#if BREWPI_SIMULATE
			return new ValueActuator();
#else
		else
			// use hardware actuators even for simulator
			return new DigitalPinActuator(config.hw.pinNr, config.hw.invert);
#endif
	case DEVICE_HARDWARE_ONEWIRE_TEMP:
#if BREWPI_SIMULATE
		return new ExternalTempSensor(false); // initially disconnected, so init doesn't populate the filters with the default value of 0.0
#else
		return new OneWireTempSensor(oneWireBus(config.hw.pinNr), config.hw.address, config.hw.calibration);
#endif

//#if BREWPI_DS2413
//	case DEVICE_HARDWARE_ONEWIRE_2413:
//#if BREWPI_SIMULATE
//		if (dt == DEVICETYPE_SWITCH_SENSOR)
//			return new ValueSensor<bool>(false);
//		else
//			return new ValueActuator();
//#else
//		return new OneWireActuator(oneWireBus(config.hw.pinNr), config.hw.address, config.hw.pio, config.hw.invert);
//#endif
//#endif
	}
	return NULL;
}

void DeviceManager::disposeDevice(DeviceType dt, void *device)
{
	switch (dt)
	{
	case DEVICETYPE_NONE:
		break;
	case DEVICETYPE_TEMP_SENSOR:
		delete (BasicTempSensor *)device;
		break;
	case DEVICETYPE_SWITCH_SENSOR:
		delete (SwitchSensor *)device;
		break;
	case DEVICETYPE_HUM_SENSOR:
		delete (HumiditySensor *)device;
		break;		
	case DEVICETYPE_SWITCH_ACTUATOR:
		delete (Actuator *)device;
		break;
	}
}

/**
 * Returns the pointer to where the device pointer resides. This can be used to delete the current device and install a new one. 
 * For Temperature sensors, the returned pointer points to a TempSensor*. The basic device can be fetched by calling
 * TempSensor::getSensor().
 */
inline void **deviceTarget(DeviceConfig &config)
{
	// for multichamber, will write directly to the multi-chamber managed storage.
	// later...
	if (config.chamber > 1 || config.beer > 1)
		return NULL;

	void **ppv;
	switch (config.deviceFunction)
	{
	case DEVICE_CHAMBER_ROOM_TEMP:
		ppv = (void **)&tempControl.ambientSensor;
		break;
	case DEVICE_CHAMBER_DOOR:
		ppv = (void **)&tempControl.door;
		break;
	case DEVICE_CHAMBER_LIGHT:
		ppv = (void **)&tempControl.light;
		break;
	case DEVICE_CHAMBER_HEAT:
		ppv = (void **)&tempControl.heater;
		break;
	case DEVICE_CHAMBER_COOL:
		ppv = (void **)&tempControl.cooler;
		break;
	case DEVICE_CHAMBER_TEMP:
		ppv = (void **)&tempControl.fridgeSensor;
		break;
	case DEVICE_CHAMBER_HUMIDITY:
		ppv = (void **)&tempControl.fridgeHumidity;
		break;		
	case DEVICE_CHAMBER_FAN:
		ppv = (void **)&tempControl.fan;
		break;
	case DEVICE_BEER_TEMP:
		ppv = (void **)&tempControl.beerSensor;
		break;
	default:
		ppv = NULL;
	}
	return ppv;
}

// A pointer to a "temp sensor" may be a TempSensor* or a BasicTempSensor* . 
// These functions allow uniform treatment.
inline bool isBasicSensor(DeviceFunction function)
{
	// currently only ambient sensor is basic. The others are wrapped in a TempSensor.
	return function == DEVICE_CHAMBER_ROOM_TEMP;
}

inline BasicTempSensor &unwrapSensor(DeviceFunction f, void *pv)
{
	return isBasicSensor(f) ? *(BasicTempSensor *)pv : ((TempSensor *)pv)->sensor();
}

inline void setSensor(DeviceFunction f, void **ppv, BasicTempSensor *sensor)
{
	if (isBasicSensor(f))
		*ppv = sensor;
	else
		((TempSensor *)*ppv)->setSensor(sensor);
}

/**
 * Removes an installed device.
 * /param config The device to remove. The fields that are used are
 *		chamber, beer, hardware and function.
 */
void DeviceManager::uninstallDevice(DeviceConfig &config)
{
	void **ppv = deviceTarget(config);
	if (ppv == NULL)
		return;

	DeviceType dt = deviceType(config.deviceFunction);
	BasicTempSensor *s;
	switch (dt)
	{
	case DEVICETYPE_NONE:
		break;
	case DEVICETYPE_TEMP_SENSOR:
		// sensor may be wrapped in a TempSensor class, or may stand alone.
		s = &unwrapSensor(config.deviceFunction, *ppv);
		if (s != &defaultTempSensor)
		{
			setSensor(config.deviceFunction, ppv, &defaultTempSensor);
			DEBUG_ONLY(logInfoInt(INFO_UNINSTALL_TEMP_SENSOR, config.deviceFunction));
			delete s;
		}
		break;
	case DEVICETYPE_SWITCH_ACTUATOR:
		if (*ppv != &defaultActuator)
		{
			DEBUG_ONLY(logInfoInt(INFO_UNINSTALL_ACTUATOR, config.deviceFunction));
			delete (Actuator *)*ppv;
			*ppv = &defaultActuator;
		}
		break;
	case DEVICETYPE_SWITCH_SENSOR:
		if (*ppv != &defaultSensor)
		{
			DEBUG_ONLY(logInfoInt(INFO_UNINSTALL_SWITCH_SENSOR, config.deviceFunction));
			delete (SwitchSensor *)*ppv;
			*ppv = &defaultSensor;
		}
		break;
	case DEVICETYPE_HUM_SENSOR:
		if (*ppv != &defaultHumiditySensor)
		{
			DEBUG_ONLY(logInfoInt(INFO_UNINSTALL_HUM_SENSOR, config.deviceFunction));
			delete (HumiditySensor *)*ppv;
			*ppv = &defaultHumiditySensor;
		}
		break;		
	}
}

/**
 * Creates and installs a device in the current chamber. 
 */
void DeviceManager::installDevice(DeviceConfig &config)
{
	DeviceType dt = deviceType(config.deviceFunction);
	void **ppv = deviceTarget(config);
	if (ppv == NULL || config.hw.deactivate)
		return;

	BasicTempSensor *s;
	TempSensor *ts;
	switch (dt)
	{
	case DEVICETYPE_NONE:
		break;
	case DEVICETYPE_TEMP_SENSOR:
		DEBUG_ONLY(logInfoInt(INFO_INSTALL_TEMP_SENSOR, config.deviceFunction));
		// sensor may be wrapped in a TempSensor class, or may stand alone.
		s = (BasicTempSensor *)createDevice(config, dt);
		if (*ppv == NULL)
		{
			logErrorInt(ERROR_OUT_OF_MEMORY_FOR_DEVICE, config.deviceFunction);
		}
		if (isBasicSensor(config.deviceFunction))
		{
			s->init();
			*ppv = s;
		}
		else
		{
			ts = ((TempSensor *)*ppv);
			ts->setSensor(s);
			ts->init();
		}
#if BREWPI_SIMULATE
		((ExternalTempSensor *)s)->setConnected(true); // now connect the sensor after init is called
#endif
		break;
	case DEVICETYPE_HUM_SENSOR:
	case DEVICETYPE_SWITCH_ACTUATOR:
	case DEVICETYPE_SWITCH_SENSOR:
		DEBUG_ONLY(logInfoInt(INFO_INSTALL_DEVICE, config.deviceFunction));
		*ppv = createDevice(config, dt);
#if (BREWPI_DEBUG > 0)
		if (*ppv == NULL)
			logErrorInt(ERROR_OUT_OF_MEMORY_FOR_DEVICE, config.deviceFunction);
#endif
		break;
	}
}

struct DeviceDefinition
{
	int8_t id;
	int8_t chamber;
	int8_t beer;
	int8_t deviceFunction;
	int8_t deviceHardware;
	int8_t pinNr;
	int8_t invert;
	int8_t pio;
	int8_t deactivate;
	int8_t calibrationAdjust;
	DeviceAddress address;

	/**
	 * Lists the first letter of the key name for each attribute.
	 */
	static const char ORDER[12];
};

// the special cases are placed at the end. All others should map directly to an int8_t via atoi().
const char DeviceDefinition::ORDER[12] = "icbfhpxndja";
const char DEVICE_ATTRIB_INDEX = 'i';
const char DEVICE_ATTRIB_CHAMBER = 'c';
const char DEVICE_ATTRIB_BEER = 'b';
const char DEVICE_ATTRIB_FUNCTION = 'f';
const char DEVICE_ATTRIB_HARDWARE = 'h';
const char DEVICE_ATTRIB_PIN = 'p';
const char DEVICE_ATTRIB_INVERT = 'x';
const char DEVICE_ATTRIB_DEACTIVATED = 'd';
const char DEVICE_ATTRIB_ADDRESS = 'a';
//#if BREWPI_DS2413
//const char DEVICE_ATTRIB_PIO = 'n';
//#endif
const char DEVICE_ATTRIB_CALIBRATEADJUST = 'j'; // value to add to temp sensors to bring to correct temperature
const char DEVICE_ATTRIB_VALUE = 'v';			// print current values
const char DEVICE_ATTRIB_WRITE = 'w';			// write value to device
const char DEVICE_ATTRIB_TYPE = 't';

void handleDeviceDefinition(const char *key, const char *val, void *pv)
{
	DeviceDefinition *def = (DeviceDefinition *)pv;
	// logDebug("deviceDef %s:%s", key, val);

	// the characters are listed in the same order as the DeviceDefinition struct.
	int8_t idx = indexOf(DeviceDefinition::ORDER, key[0]);
	if (key[0] == DEVICE_ATTRIB_ADDRESS)
		parseBytes(def->address, val, 8);
	else if (key[0] == DEVICE_ATTRIB_CALIBRATEADJUST)
	{
		temperature parsedVal;
		if (stringToTempDiff(&parsedVal, val))
		{
			def->calibrationAdjust = fixed4_4(parsedVal >> (TEMP_FIXED_POINT_BITS - CALIBRATION_OFFSET_PRECISION));
		}
	}
	else if (idx >= 0)
		((uint8_t *)def)[idx] = (uint8_t)atol(val);
}

bool inRangeUInt8(uint8_t val, uint8_t min, int8_t max)
{
	return min <= val && val <= max;
}

bool inRangeInt8(int8_t val, int8_t min, int8_t max)
{
	return min <= val && val <= max;
}

void assignIfSet(int8_t value, uint8_t *target)
{
	if (value >= 0)
		*target = (uint8_t)value;
}

/**
 * Updates the device definition. Only changes that result in a valid device, with no conflicts with other devices
 * are allowed. 
 */
void DeviceManager::parseDeviceDefinition(Stream &p)
{
	static DeviceDefinition dev;
	fill((int8_t *)&dev, sizeof(dev));

	piLink.parseJson(&handleDeviceDefinition, &dev);

	if (!inRangeInt8(dev.id, 0, MAX_DEVICE_SLOT)) // no device id given, or it's out of range, can't do anything else.
		return;

#ifdef FORCE_DEVICE_DEFAULTS
	// If FORCE_DEVICE_DEFAULTS is set, overwrite the chamber/beer number to prevent user error. From @Thorrak
	dev.chamber = 1;
	if (dev.deviceFunction >= 9 && dev.deviceFunction <= 15)
		dev.beer = 1;
	else
		dev.beer = 0;
#endif

	// save the original device so we can revert
	DeviceConfig target;
	DeviceConfig original;

	// todo - should ideally check if the eeprom is correctly initialized.
	eepromManager.fetchDevice(original, dev.id);
	memcpy(&target, &original, sizeof(target));

	assignIfSet(dev.chamber, &target.chamber);
	assignIfSet(dev.beer, &target.beer);
	assignIfSet(dev.deviceFunction, (uint8_t *)&target.deviceFunction);
	assignIfSet(dev.deviceHardware, (uint8_t *)&target.deviceHardware);
	assignIfSet(dev.pinNr, &target.hw.pinNr);

//#if BREWPI_DS2413
//	assignIfSet(dev.pio, &target.hw.pio);
//#endif

	if (dev.calibrationAdjust != -1) // since this is a union, it also handles pio for 2413 sensors
		target.hw.calibration = dev.calibrationAdjust;

	assignIfSet(dev.invert, (uint8_t *)&target.hw.invert);

	if (dev.address[0] != 0xFF) // first byte is family identifier. I don't have a complete list, but so far 0xFF is not used.
		memcpy(target.hw.address, dev.address, 8);

	assignIfSet(dev.deactivate, (uint8_t *)&target.hw.deactivate);

	// setting function to none clears all other fields.
	if (target.deviceFunction == DEVICE_NONE)
	{
		clear((uint8_t *)&target, sizeof(target));
	}

	bool valid = isDeviceValid(target, original, dev.id);
	DeviceConfig *print = &original;
	if (valid)
	{
		print = &target;
		// remove the device associated with the previous function
		uninstallDevice(original);
		// also remove any existing device for the new function, since install overwrites any existing definition.
		uninstallDevice(target);
		installDevice(target);
		eepromManager.storeDevice(target, dev.id);
	}
	else
	{
		logError(ERROR_DEVICE_DEFINITION_UPDATE_SPEC_INVALID);
	}
	piLink.printResponse('U');
	deviceManager.beginDeviceOutput();
	deviceManager.printDevice(dev.id, *print, NULL, p);
	piLink.printNewLine();
}

/**
 * Determines if a given device definition is valid.
 * chamber/beer must be within bounds
 * device function must match the chamber/beer spec, and must not already be defined for the same chamber/beer combination
 * device hardware type must be applicable with the device function
 * pinNr must be unique for digital pin devices?
 * pinNr must be a valid OneWire bus for one wire devices.
 * for onewire temp devices, address must be unique.
 * for onewire ds2413 devices, address+pio must be unique.
 */
bool DeviceManager::isDeviceValid(DeviceConfig &config, DeviceConfig &original, uint8_t deviceIndex)
{
#if 1
	/* Implemented checks to ensure the system will not crash when supplied with invalid data.
	   More refined checks that may cause confusing results are not yet implemented. See todo below. */

	/* chamber and beer within range.*/
	if (!inRangeUInt8(config.chamber, 0, EepromFormat::MAX_CHAMBERS))
	{
		logErrorInt(ERROR_INVALID_CHAMBER, config.chamber);
		return false;
	}

	/* 0 is allowed - represents a chamber device not assigned to a specific beer */
	if (!inRangeUInt8(config.beer, 0, ChamberBlock::MAX_BEERS))
	{
		logErrorInt(ERROR_INVALID_BEER, config.beer);
		return false;
	}

	if (!inRangeUInt8(config.deviceFunction, 0, DEVICE_MAX - 1))
	{
		logErrorInt(ERROR_INVALID_DEVICE_FUNCTION, config.deviceFunction);
		return false;
	}

	DeviceOwner owner = deviceOwner(config.deviceFunction);
	if (!((owner == DEVICE_OWNER_BEER && config.beer) || (owner == DEVICE_OWNER_CHAMBER && config.chamber) || (owner == DEVICE_OWNER_NONE && !config.beer && !config.chamber)))
	{
		logErrorIntIntInt(ERROR_INVALID_DEVICE_CONFIG_OWNER, owner, config.beer, config.chamber);
		return false;
	}

	// todo - find device at another index with the same chamber/beer/function spec.
	// with duplicate function defined for the same beer, that they will both try to create/delete the device in the target location.
	// The highest id will win.
	DeviceType dt = deviceType(config.deviceFunction);
	if (!isAssignable(dt, config.deviceHardware))
	{
		logErrorIntInt(ERROR_CANNOT_ASSIGN_TO_HARDWARE, dt, config.deviceHardware);
		return false;
	}

	// todo - check pinNr uniqueness for direct digital I/O devices?

	/* pinNr for a onewire device must be a valid bus. While this won't cause a crash, it's a good idea to validate this. */
	if (isOneWire(config.deviceHardware))
	{
		if (!oneWireBus(config.hw.pinNr))
		{
			logErrorInt(ERROR_NOT_ONEWIRE_BUS, config.hw.pinNr);
			return false;
		}
	}
	else
	{   // regular pin device
		// todo - could verify that the pin nr corresponds to enumActuatorPins/enumSensorPins
	}

#endif
	// todo - for onewire temp, ensure address is unique
	// todo - for onewire 2413 check address+pio nr is unique
	return true;
}

void printAttrib(Print &p, char c, int8_t val, bool first = false)
{
	if (!first)
		p.print(',');

	char tempString[32]; // resulting string limited to 128 chars
	sprintf_P(tempString, PSTR("\"%c\":%d"), c, val);
	p.print(tempString);
}

inline bool hasInvert(DeviceHardware hw)
{
	return hw == DEVICE_HARDWARE_PIN
//#if BREWPI_DS2413
//		   || hw == DEVICE_HARDWARE_ONEWIRE_2413
//#endif
		;
}

inline bool hasOnewire(DeviceHardware hw)
{
	return
//#if BREWPI_DS2413
//		hw == DEVICE_HARDWARE_ONEWIRE_2413 ||
//#endif
		hw == DEVICE_HARDWARE_ONEWIRE_TEMP;
}

void DeviceManager::printDevice(device_slot_t slot, DeviceConfig &config, const char *value, Print &p)
{
	char buf[17];

	DeviceType dt = deviceType(config.deviceFunction);
	if (!firstDeviceOutput)
	{
		// p.print('\n');
		p.print(',');
	}
	firstDeviceOutput = false;
	p.print('{');
	printAttrib(p, DEVICE_ATTRIB_INDEX, slot, true);
	printAttrib(p, DEVICE_ATTRIB_TYPE, dt);

	printAttrib(p, DEVICE_ATTRIB_CHAMBER, config.chamber);
	printAttrib(p, DEVICE_ATTRIB_BEER, config.beer);
	printAttrib(p, DEVICE_ATTRIB_FUNCTION, config.deviceFunction);
	printAttrib(p, DEVICE_ATTRIB_HARDWARE, config.deviceHardware);
	printAttrib(p, DEVICE_ATTRIB_DEACTIVATED, config.hw.deactivate);
	printAttrib(p, DEVICE_ATTRIB_PIN, config.hw.pinNr);
	if (value && *value)
	{
		p.print(",\"v\":");
		p.print(value);
	}
	if (hasInvert(config.deviceHardware))
		printAttrib(p, DEVICE_ATTRIB_INVERT, config.hw.invert);

	if (hasOnewire(config.deviceHardware))
	{
		p.print(",\"a\":\"");
		printBytes(config.hw.address, 8, buf);
		p.print(buf);
		p.print('"');
	}
//#if BREWPI_DS2413
//	if (config.deviceHardware == DEVICE_HARDWARE_ONEWIRE_2413)
//	{
//		printAttrib(p, DEVICE_ATTRIB_PIO, config.hw.pio);
//	}
//#endif
	if (config.deviceHardware == DEVICE_HARDWARE_ONEWIRE_TEMP)
	{
		tempDiffToString(buf, temperature(config.hw.calibration) << (TEMP_FIXED_POINT_BITS - CALIBRATION_OFFSET_PRECISION), 3, 8);
		p.print(",\"j\":");
		p.print(buf);
	}
	p.print('}');
}

bool DeviceManager::allDevices(DeviceConfig &config, uint8_t deviceIndex)
{
	return eepromManager.fetchDevice(config, deviceIndex);
}

void parseBytes(uint8_t *data, const char *s, uint8_t len)
{
	char c;
	while ((c = *s++))
	{
		uint8_t d = (c >= 'A' ? c - 'A' + 10 : c - '0') << 4;
		c = *s++;
		d |= (c >= 'A' ? c - 'A' + 10 : c - '0');
		*data++ = d;
	}
}

void printBytes(const uint8_t *data, uint8_t len, char *buf) // prints 8-bit data in hex
{
	for (int i = 0; i < len; i++)
	{
		uint8_t b = (data[i] >> 4) & 0x0f;
		*buf++ = (b > 9 ? b - 10 + 'A' : b + '0');
		b = data[i] & 0x0f;
		*buf++ = (b > 9 ? b - 10 + 'A' : b + '0');
	}
	*buf = 0;
}

void DeviceManager::OutputEnumeratedDevices(DeviceConfig *config, DeviceCallbackInfo *info)
{
	printDevice(info->slot, *config, info->value, *(Print *)info->data);
}

bool DeviceManager::enumDevice(DeviceDisplay &dd, DeviceConfig &dc, uint8_t idx)
{
	if (dd.id == -1)
		return (dd.empty || dc.deviceFunction); // if enumerating all devices, honor the unused request param
	else
		return (dd.id == idx); // enumerate only the specific device requested
}

void handleHardwareSpec(const char *key, const char *val, void *pv)
{
	EnumerateHardware *h = (EnumerateHardware *)pv;
	// logDebug("hardwareSpec %s:%s", key, val);

	int8_t idx = indexOf("hpvuf", key[0]);
	if (idx >= 0)
	{
		*((int8_t *)h + idx) = atol(val);
	}
}

inline bool matchAddress(uint8_t *detected, uint8_t *configured, uint8_t count)
{
	if (!configured[0])
		return true;
	while (count-- > 0)
	{
		if (detected[count] != configured[count])
			return false;
	}
	return true;
}

/**
 * Find a device based on it's location.
 * A device's location is:
 *   pinNr  for simple digital pin devices
 *   pinNr+address for one-wire devices
 *   pinNr+address+pio for 2413
 */
device_slot_t findHardwareDevice(DeviceConfig &find)
{
	DeviceConfig config;
	for (device_slot_t slot = 0; deviceManager.allDevices(config, slot); slot++)
	{
		if (find.deviceHardware == config.deviceHardware)
		{
			bool match = true;
			switch (find.deviceHardware)
			{
//#if BREWPI_DS2413
//			case DEVICE_HARDWARE_ONEWIRE_2413:
//				match &= find.hw.pio == config.hw.pio;
//				// fall through
//#endif
			case DEVICE_HARDWARE_ONEWIRE_TEMP:
				match &= matchAddress(find.hw.address, config.hw.address, 8);
				// fall through
			case DEVICE_HARDWARE_PIN:
				match &= find.hw.pinNr == config.hw.pinNr;
			default: // this should not happen - if it does the device will be returned as matching.
				break;
			}
			if (match)
				return slot;
		}
	}
	return INVALID_SLOT;
}

inline void DeviceManager::readTempSensorValue(DeviceConfig::Hardware hw, char *out)
{
#if !BREWPI_SIMULATE
	OneWire *bus = oneWireBus(hw.pinNr);
	OneWireTempSensor sensor(bus, hw.address, 0); // NB: this value is uncalibrated, since we don't have the calibration offset until the device is configured
	temperature temp = INVALID_TEMP;
	if (sensor.init())
		temp = sensor.read();
	tempToString(out, temp, 3, 9);
#else
	strcpy_P(out, PSTR("0.00"));
#endif
}

void DeviceManager::handleEnumeratedDevice(DeviceConfig &config, EnumerateHardware &h, EnumDevicesCallback callback, DeviceCallbackInfo *info)
{
	if (h.function && !isAssignable(deviceType(DeviceFunction(h.function)), config.deviceHardware))
		return; // device not applicable for required function

	// logDebug("Handling device");
	device_slot_t slot = findHardwareDevice(config);
	info->slot = slot;
	info->value[0] = 0;
	DEBUG_ONLY(logInfoInt(INFO_MATCHING_DEVICE, slot));

	if (isDefinedSlot(slot))
	{
		if (h.unused) // only list unused devices, and this one is already used
			return;
		// display the actual matched value
		deviceManager.allDevices(config, slot);
	}

	if (h.values)
	{
		// logDebug("Fetching device value");
		switch (config.deviceHardware)
		{
		case DEVICE_HARDWARE_ONEWIRE_TEMP:
			readTempSensorValue(config.hw, info->value);
			break;
		// unassigned pins could be input or output so we can't determine any other details from here.
		// values can be read once the pin has been assigned a function
		default:
			break;
		}
	}
	// logDebug("Passing device to callback");
	callback(&config, info);
}

/**
 * Enumerate the static devices that are permanently installed.
 * @param h
 * @param callback
 * @param output
 */
void DeviceManager::enumeratePinDevices(EnumerateHardware &h, EnumDevicesCallback callback, DeviceCallbackInfo *info)
{
	DeviceConfig config;
	clear((uint8_t *)&config, sizeof(config));
	config.deviceHardware = DEVICE_HARDWARE_PIN;
	config.chamber = 1; // chamber 1 is default

	int8_t pin;
#if BREWPI_ACTUATOR_PINS
	for (uint8_t count = 0; (pin = deviceManager.enumerateActuatorPins(count)) >= 0; count++)
	{
		if (h.pin != -1 && h.pin != pin)
			continue;
		config.hw.pinNr = pin;
		config.hw.invert = true; // make inverted default, because Arduino shields have transistor on them
		handleEnumeratedDevice(config, h, callback, info);
	}
#endif

#if BREWPI_SENSOR_PINS
	for (uint8_t count = 0; (pin = deviceManager.enumerateSensorPins(count)) >= 0; count++)
	{
		if (h.pin != -1 && h.pin != pin)
			continue;
		config.hw.pinNr = pin;
		handleEnumeratedDevice(config, h, callback, info);
	}
#endif
	for (uint8_t count = 0; (pin = deviceManager.enumerateHumidityPins(count)) >= 0; count++)
	{
		if (h.pin != -1 && h.pin != pin)
			continue;
		config.hw.pinNr = pin;
		// config.hw.invert = true; // make inverted default, because Arduino shields have transistor on them
		handleEnumeratedDevice(config, h, callback, info);
	}

}

void DeviceManager::enumerateOneWireDevices(EnumerateHardware &h, EnumDevicesCallback callback, DeviceCallbackInfo *info)
{
#if !BREWPI_SIMULATE
	int8_t pin;
	for (uint8_t count = 0; (pin = deviceManager.enumOneWirePins(count)) >= 0; count++)
	{
		DeviceConfig config;
		clear((uint8_t *)&config, sizeof(config));
		if (h.pin != -1 && h.pin != pin)
			continue;
		config.hw.pinNr = pin;
		config.chamber = 1; // chamber 1 is default
							// logDebug("Enumerating one-wire devices on pin %d", pin);
		OneWire *wire = oneWireBus(pin);
		if (wire != NULL)
		{
			wire->reset_search();
			while (wire->search(config.hw.address))
			{
				// hardware device type from OneWire family ID
				switch (config.hw.address[0])
				{
//#if BREWPI_DS2413
//				case DS2413_FAMILY_ID:
//					config.deviceHardware = DEVICE_HARDWARE_ONEWIRE_2413;
//					break;
//#endif
				case DS18B20MODEL:
					config.deviceHardware = DEVICE_HARDWARE_ONEWIRE_TEMP;
					break;
				default:
					config.deviceHardware = DEVICE_HARDWARE_NONE;
				}

				switch (config.deviceHardware)
				{
// #if BREWPI_DS2413
// 				// for 2408 this will require iterating 0..7
// 				case DEVICE_HARDWARE_ONEWIRE_2413:
// 					// enumerate each pin separately
// 					for (uint8_t i = 0; i < 2; i++)
// 					{
// 						config.hw.pio = i;
// 						handleEnumeratedDevice(config, h, callback, info);
// 					}
// 					break;
// #endif
				case DEVICE_HARDWARE_ONEWIRE_TEMP:
#if !ONEWIRE_PARASITE_SUPPORT
				{ // check that device is not parasite powered
					DallasTemperature sensor(wire);
					// initialize sensor without reset detection (faster)
					if (!sensor.isParasitePowered(config.hw.address))
					{
						handleEnumeratedDevice(config, h, callback, info);
					}
				}
#else
					handleEnumeratedDevice(config, h, callback, info);
#endif
				break;
				default:
					handleEnumeratedDevice(config, h, callback, info);
				}
			}
		}
	}
#endif
}

void DeviceManager::enumerateHardwareToStream(Stream &p)
{
	EnumerateHardware spec;
	// set up defaults
	spec.unused = 0;	// list all devices
	spec.values = 0;	// don't list values
	spec.pin = -1;		// any pin
	spec.hardware = -1; // any hardware
	spec.function = 0;  // no function restriction

	piLink.parseJson(handleHardwareSpec, &spec);
	DeviceCallbackInfo info;
	info.data = &p;

	// logDebug("Enumerating Hardware");

	firstDeviceOutput = true;
	enumerateHardware(spec, OutputEnumeratedDevices, &info);
	// logDebug("Enumerating Hardware Complete");
}

void DeviceManager::enumerateHardware(EnumerateHardware &spec, EnumDevicesCallback callback, DeviceCallbackInfo *info)
{
	if (spec.hardware == -1 || isOneWire(DeviceHardware(spec.hardware)))
	{
		enumerateOneWireDevices(spec, callback, info);
	}
	if (spec.hardware == -1 || isDigitalPin(DeviceHardware(spec.hardware)))
	{
		enumeratePinDevices(spec, callback, info);
	}
}

void HandleDeviceDisplay(const char *key, const char *val, void *pv)
{
	DeviceDisplay &dd = *(DeviceDisplay *)pv;
	//logDeveloper("DeviceDisplay %s:%s"), key, val);

	int8_t idx = indexOf("irwe", key[0]);
	if (idx >= 0)
	{
		*((int8_t *)&dd + idx) = atol(val);
	}
}

void UpdateDeviceState(DeviceDisplay &dd, DeviceConfig &dc, char *val)
{
	DeviceType dt = deviceType(dc.deviceFunction);
	if (dt == DEVICETYPE_NONE)
		return;

	void **ppv = deviceTarget(dc);
	if (ppv == NULL)
		return;

	if (dd.write >= 0 && dt == DEVICETYPE_SWITCH_ACTUATOR)
	{
		// write value to a specific device. For now, only actuators are relevant targets
		//DEBUG_ONLY(logInfoInt(INFO_SETTING_ACTIVATOR_STATE, dd.write != 0));
		((Actuator *)*ppv)->setActive(dd.write != 0);
	}
	else if (dd.value == 1)
	{ // read values
		if (dt == DEVICETYPE_SWITCH_SENSOR)
		{
			sprintf_P(val, STR_FMT_U, (unsigned int)((SwitchSensor *)*ppv)->sense() != 0); // cheaper than itoa, because it overlaps with vsnprintf
		}
		else if (dt == DEVICETYPE_TEMP_SENSOR)
		{
			BasicTempSensor &s = unwrapSensor(dc.deviceFunction, *ppv);
			temperature temp = s.read();
			tempToString(val, temp, 3, 9);
		}
		else if (dt == DEVICETYPE_SWITCH_ACTUATOR)
		{
			sprintf_P(val, STR_FMT_U, (unsigned int)((Actuator *)*ppv)->isActive() != 0);
		}
		else if (dt == DEVICETYPE_HUM_SENSOR)
		{
			HumiditySensor &s = *(HumiditySensor *)*ppv;
			humidity hum = s.read();
			tempToString(val, hum, 3, 9);
		}
	}
}

void DeviceManager::listDevices(Stream &p)
{
	DeviceConfig dc;
	DeviceDisplay dd;
	fill((int8_t *)&dd, sizeof(dd));
	dd.empty = 0;
	piLink.parseJson(HandleDeviceDisplay, (void *)&dd);
	if (dd.id == -2)
	{
		if (dd.write >= 0)
			tempControl.cameraLight.setActive(dd.write != 0);
		return;
	}
	deviceManager.beginDeviceOutput();
	for (device_slot_t idx = 0; deviceManager.allDevices(dc, idx); idx++)
	{
		if (deviceManager.enumDevice(dd, dc, idx))
		{
			char val[10];
			val[0] = 0;
			UpdateDeviceState(dd, dc, val);
			deviceManager.printDevice(idx, dc, val, p);
		}
	}
}

/**
 * Determines the class of device for the given DeviceID.
 */
DeviceType deviceType(DeviceFunction id)
{
	switch (id)
	{
	case DEVICE_CHAMBER_DOOR:
		return DEVICETYPE_SWITCH_SENSOR;

	case DEVICE_CHAMBER_HEAT:
	case DEVICE_CHAMBER_COOL:
	case DEVICE_CHAMBER_LIGHT:
	case DEVICE_CHAMBER_FAN:
	case DEVICE_BEER_HEAT:
	case DEVICE_BEER_COOL:
		return DEVICETYPE_SWITCH_ACTUATOR;

	case DEVICE_CHAMBER_HUMIDITY:
		return DEVICETYPE_HUM_SENSOR;

	case DEVICE_CHAMBER_TEMP:
	case DEVICE_CHAMBER_ROOM_TEMP:
	case DEVICE_BEER_TEMP:
	case DEVICE_BEER_TEMP2:
		return DEVICETYPE_TEMP_SENSOR;

	default:
		return DEVICETYPE_NONE;
	}
}

DeviceManager deviceManager;
