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
#include "DeviceManager.h"
#include "TempControl.h"

struct ChamberSettings
{
	ControlConstants cc;
	uint8_t reserved[1]; // was 3, but added pidMax
};

struct BeerBlock
{
	ControlSettings cs;
	uint8_t reserved[2];
};

struct ChamberBlock
{
	static const uint8_t MAX_BEERS = 6;
	ChamberSettings chamberSettings;
	BeerBlock beer[MAX_BEERS];
};

struct EepromFormat
{
	static const uint16_t MAX_EEPROM_SIZE = 1024;
	static const uint8_t MAX_CHAMBERS = 4;
	static const uint8_t MAX_DEVICES = MAX_DEVICE_SLOT;

	uint8_t version;
	uint8_t numChambers; // todo - remove this - and increase reserved space.
	uint8_t reserved[4];
	ChamberBlock chambers[MAX_CHAMBERS];
	DeviceConfig devices[MAX_DEVICES];
};

// check at compile time that the structure will fit into eeprom
void eepromSizeTooLarge()
	__attribute__((error("EEPROM data is > 1024 bytes")));

static inline __attribute__((always_inline)) void eepromSizeCheck()
{
	if (sizeof(EepromFormat) > EepromFormat::MAX_EEPROM_SIZE)
	{
		eepromSizeTooLarge();
	}
}

/**  
 * If the eeprom data is not initialized or is not the same version as
 * expected, all chambers go until the valid data is provided. This is
 * done by making the default mode offline. The external script will
 * either reset the eeprom settings or manage the upgrade between
 * versions. Note that this is typically only necessary after flashing
 * new firmware, which is usually watched by an operator. If the arduino
 * restarts after a power failure, the settings will have been upgraded
 * and operation can continue from the saved settings.
 */

/*
 * Increment this value each time a change is made that is not
 * backwardly-compatible. Either the eeprom will be reset to defaults,
 * or external code will re-establish the values via the piLink
 * interface. 
 */
#define EEPROM_FORMAT_VERSION 5

/*
 * Version history:
 *
 * rev 1: static config (original avr code)
 * rev 2: initial version dynaconfig
 * rev 3: deactivate flag in DeviceConfig, and additional padding to allow
 *        for some future expansion.
 * rev 4: added padding at start and reduced device count to 16. We can always
 *        increase later.
 * rev 5: added humidity sensor.
 */
