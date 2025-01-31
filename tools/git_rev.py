#!/usr/bin/python

# Copyright (C) 2018, 2019 Lee C. Bussy (@LBussy)

# This file is part of LBussy's BrewPi Firmware Remix (BrewPi-Firmware-RMX).
#
# BrewPi Firmware RMX is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# BrewPi Firmware RMX is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with BrewPi Firmware RMX. If not, see <https://www.gnu.org/licenses/>.

import subprocess

# Get 0.0.0 version from latest Git tag
tagcmd = "git describe --tags --abbrev=0"
version = subprocess.check_output(tagcmd, shell=True).decode().strip()

# Get latest commit short from Git
revcmd = "git log --pretty=format:'%h' -n 1"
commit = subprocess.check_output(revcmd, shell=True).decode().strip()

# Make both available for use in the defines
print("-D PIO_SRC_TAG={0}".format(version))
print("-D PIO_SRC_REV={0}".format(commit))
