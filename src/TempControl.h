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

#pragma once

#include "Brewpi.h"
#include "TemperatureFormats.h"
#include "Platform.h"
#include "TempSensor.h"
#include "HumiditySensor.h"
#include "Actuator.h"
#include "Sensor.h"
#include "EepromTypes.h"
#include "ActuatorAutoOff.h"
#include "ModeControl.h"
#include "TicksImpl.h"

#if BREWPI_STATIC_CONFIG != BREWPI_SHIELD_GLYCOL
// These are regular BrewPi constants:
//
// Set minimum off time to prevent short cycling the compressor in seconds
const uint16_t MIN_COOL_OFF_TIME = 300;
// Use a minimum off time for the heater as well, so it heats in cycles, not
// lots of short bursts
const uint16_t MIN_HEAT_OFF_TIME = 300;
// Minimum on time for the cooler.
const uint16_t MIN_COOL_ON_TIME = 180;
// Minimum on time for the heater.
const uint16_t MIN_HEAT_ON_TIME = 180;
// Use a large minimum off time in fridge constant mode. No need for very
// fast cycling.
const uint16_t MIN_COOL_OFF_TIME_FRIDGE_CONSTANT = 600;
// Set a minimum off time between switching between heating and cooling
const uint16_t MIN_SWITCH_TIME = 600;
// Time allowed for peak detection
const uint16_t COOL_PEAK_DETECT_TIME = 1800;
const uint16_t HEAT_PEAK_DETECT_TIME = 900;
#else
// Glycol support constants
const uint16_t MIN_COOL_OFF_TIME = 0;
const uint16_t MIN_HEAT_OFF_TIME = 30;
const uint16_t MIN_COOL_ON_TIME = 0;
const uint16_t MIN_HEAT_ON_TIME = 30;
const uint16_t MIN_COOL_OFF_TIME_FRIDGE_CONSTANT = 0;
const uint16_t MIN_SWITCH_TIME = 30;
const uint16_t COOL_PEAK_DETECT_TIME = 180;
const uint16_t HEAT_PEAK_DETECT_TIME = 180;
#endif

// These two structs are stored in and loaded from EEPROM
struct ControlSettings
{
	control_mode_t mode;
	temperature beerSetting;
	temperature fridgeSetting;
	temperature heatEstimator; // updated automatically by self learning algorithm
	temperature coolEstimator; // updated automatically by self learning algorithm
};

struct ControlVariables
{
	temperature beerDiff;
	long_temperature diffIntegral; // also uses 9 fraction bits, but more integer bits to prevent overflow
	temperature beerSlope;
	temperature p;
	temperature i;
	temperature d;
	temperature estimatedPeak;
	temperature negPeakEstimate; // last estimate
	temperature posPeakEstimate;
	temperature negPeak; // last detected peak
	temperature posPeak;
};

struct ControlConstants
{
	char tempFormat;
	temperature tempSettingMin;
	temperature tempSettingMax;
	temperature Kp;
	temperature Ki;
	temperature Kd;
	temperature iMaxError;
	temperature idleRangeHigh;
	temperature idleRangeLow;
	temperature heatingTargetUpper;
	temperature heatingTargetLower;
	temperature coolingTargetUpper;
	temperature coolingTargetLower;
	uint16_t maxHeatTimeForEstimate; // max time for heat estimate in seconds
	uint16_t maxCoolTimeForEstimate; // max time for heat estimate in seconds
	// for the filter coefficients the b value is stored. a is calculated from b.
	uint8_t fridgeFastFilter;  // for display, logging and on-off control
	uint8_t fridgeSlowFilter;  // for peak detection
	uint8_t fridgeSlopeFilter; // not used in current control algorithm
	uint8_t beerFastFilter;	// for display and logging
	uint8_t beerSlowFilter;	// for on/off control algorithm
	uint8_t beerSlopeFilter;   // for PID calculation
	uint8_t lightAsHeater;	 // use the light to heat rather than the configured heater device
	uint8_t rotaryHalfSteps;   // define whether to use full or half steps for the rotary encoder
	temperature pidMax;
};

#define EEPROM_TC_SETTINGS_BASE_ADDRESS 0
#define EEPROM_CONTROL_SETTINGS_ADDRESS (EEPROM_TC_SETTINGS_BASE_ADDRESS + sizeof(uint8_t))
#define EEPROM_CONTROL_CONSTANTS_ADDRESS (EEPROM_CONTROL_SETTINGS_ADDRESS + sizeof(ControlSettings))

enum states
{
	IDLE,					 // 0
	STATE_OFF,				 // 1
	DOOR_OPEN,				 // 2 used by the Display only
	HEATING,				 // 3
	COOLING,				 // 4
	WAITING_TO_COOL,		 // 5
	WAITING_TO_HEAT,		 // 6
	WAITING_FOR_PEAK_DETECT, // 7
	COOLING_MIN_TIME,		 // 8
	HEATING_MIN_TIME,		 // 9
	NUM_STATES
};

#define TC_STATE_MASK 0x7; // 3 bits

#if TEMP_CONTROL_STATIC
#define TEMP_CONTROL_METHOD static
#define TEMP_CONTROL_FIELD static
#else
#define TEMP_CONTROL_METHOD
#define TEMP_CONTROL_FIELD
#endif

// Making all functions and variables static reduces code size.
// There will only be one TempControl object, so it makes sense that they
// are static.

/*
 * MDM: To support multi-chamber, I could have made TempControl non-static,
 * and had a reference to the current instance. But this means each lookup
 * of a field must be done indirectly, which adds to the code size. Instead,
 * we swap in/out the sensors and control data so that the bulk of the code
 * can work against compile-time resolvable memory references. While the
 * design goes against the grain of typical OO practices, the reduction in
 * code size make it worth it.
 */

class TempControl
{
  public:
	TempControl(){};
	~TempControl(){};

	TEMP_CONTROL_METHOD void init(void);
	TEMP_CONTROL_METHOD void reset(void);

	TEMP_CONTROL_METHOD void updateTemperatures(void);
	TEMP_CONTROL_METHOD void updatePID(void);
	TEMP_CONTROL_METHOD void updateState(void);
	TEMP_CONTROL_METHOD void updateOutputs(void);
	TEMP_CONTROL_METHOD void detectPeaks(void);

	TEMP_CONTROL_METHOD void loadSettings(eptr_t offset);
	TEMP_CONTROL_METHOD void storeSettings(eptr_t offset);
	TEMP_CONTROL_METHOD void loadDefaultSettings(void);

	TEMP_CONTROL_METHOD void loadConstants(eptr_t offset);
	TEMP_CONTROL_METHOD void storeConstants(eptr_t offset);
	TEMP_CONTROL_METHOD void loadDefaultConstants(void);

	//TEMP_CONTROL_METHOD void loadSettingsAndConstants(void);

	TEMP_CONTROL_METHOD tcduration_t timeSinceCooling(void);
	TEMP_CONTROL_METHOD tcduration_t timeSinceHeating(void);
	TEMP_CONTROL_METHOD tcduration_t timeSinceIdle(void);

	TEMP_CONTROL_METHOD temperature getBeerTemp(void);
	TEMP_CONTROL_METHOD temperature getBeerSetting(void);
	TEMP_CONTROL_METHOD void setBeerTemp(temperature newTemp);

	TEMP_CONTROL_METHOD temperature getFridgeTemp(void);
	TEMP_CONTROL_METHOD temperature getFridgeSetting(void);
	TEMP_CONTROL_METHOD void setFridgeTemp(temperature newTemp);

	TEMP_CONTROL_METHOD humidity getFridgeHumidity(void);
	TEMP_CONTROL_METHOD temperature getRoomTemp(void)
	{
		return ambientSensor->read();
	}

	TEMP_CONTROL_METHOD void setMode(control_mode_t newMode, bool force = false);
	TEMP_CONTROL_METHOD control_mode_t getMode(void)
	{
		return cs.mode;
	}

	TEMP_CONTROL_METHOD states getState(void)
	{
		return state;
	}

	TEMP_CONTROL_METHOD tcduration_t getWaitTime(void)
	{
		return waitTime;
	}

	TEMP_CONTROL_METHOD void resetWaitTime(void)
	{
		waitTime = 0;
	}

	// TEMP_CONTROL_METHOD void updateWaitTime(uint16_t newTimeLimit, uint16_t newTimeSince);
	TEMP_CONTROL_METHOD void updateWaitTime(tcduration_t newTimeLimit, tcduration_t newTimeSince)
	{
		if (newTimeSince < newTimeLimit)
		{
			uint16_t newWaitTime = newTimeLimit - newTimeSince;
			if (newWaitTime > waitTime)
			{
				waitTime = newWaitTime;
			}
		}
	}

	TEMP_CONTROL_METHOD bool stateIsCooling(void);
	TEMP_CONTROL_METHOD bool stateIsHeating(void);
	TEMP_CONTROL_METHOD bool modeIsBeer(void)
	{
		return (cs.mode == MODE_BEER_CONSTANT || cs.mode == MODE_BEER_PROFILE);
	}

	TEMP_CONTROL_METHOD void initFilters();

	TEMP_CONTROL_METHOD bool isDoorOpen() { return doorOpen; }

	TEMP_CONTROL_METHOD unsigned char getDisplayState()
	{
		return isDoorOpen() ? DOOR_OPEN : getState();
	}

  private:
	TEMP_CONTROL_METHOD void increaseEstimator(temperature *estimator, temperature error);
	TEMP_CONTROL_METHOD void decreaseEstimator(temperature *estimator, temperature error);

	TEMP_CONTROL_METHOD void updateEstimatedPeak(uint16_t estimate, temperature estimator, uint16_t sinceIdle);

  public:
	TEMP_CONTROL_FIELD TempSensor *beerSensor;
	TEMP_CONTROL_FIELD TempSensor *fridgeSensor;
	TEMP_CONTROL_FIELD HumiditySensor *fridgeHumidity;
	TEMP_CONTROL_FIELD BasicTempSensor *ambientSensor;
	TEMP_CONTROL_FIELD Actuator *heater;
	TEMP_CONTROL_FIELD Actuator *cooler;
	TEMP_CONTROL_FIELD Actuator *light;
	TEMP_CONTROL_FIELD Actuator *fan;
	TEMP_CONTROL_FIELD AutoOffActuator cameraLight;
	TEMP_CONTROL_FIELD Sensor<bool> *door;

	// Control parameters
	TEMP_CONTROL_FIELD ControlConstants cc;
	TEMP_CONTROL_FIELD ControlSettings cs;
	TEMP_CONTROL_FIELD ControlVariables cv;

	// Defaults for control constants. Defined in cpp file, copied with memcpy_p
	static const ControlConstants ccDefaults;

  private:
	// keep track of beer setting stored in EEPROM
	TEMP_CONTROL_FIELD temperature storedBeerSetting;

	// Timers
	TEMP_CONTROL_FIELD tcduration_t lastIdleTime;
	TEMP_CONTROL_FIELD tcduration_t lastHeatTime;
	TEMP_CONTROL_FIELD tcduration_t lastCoolTime;
	TEMP_CONTROL_FIELD tcduration_t waitTime;

	// State variables
	TEMP_CONTROL_FIELD states state;
	TEMP_CONTROL_FIELD bool doPosPeakDetect;
	TEMP_CONTROL_FIELD bool doNegPeakDetect;
	TEMP_CONTROL_FIELD bool doorOpen;

	friend class TempControlState;
};

extern TempControl tempControl;
