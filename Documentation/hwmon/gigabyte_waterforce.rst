 .. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver gigabyte-waterforce
=================================

Supported devices:

* Gigabyte AORUS WATERFORCE X240
* Gigabyte AORUS WATERFORCE X280
* Gigabyte AORUS WATERFORCE X360

Author: Aleksa Savic

Description
-----------

This driver enables hardware monitoring support for the listed Gigabyte Waterforce
all-in-one CPU liquid coolers. They expose liquid temperature, as well as pump and
fan speed in RPM.

The addressable RGB LEDs and LCD screen are not supported in this driver and should
be controlled through userspace tools.

Usage Notes
-----------

As these are USB HIDs, the driver can be loaded automatically by the kernel and
supports hot swapping.

Sysfs entries
-------------

=========== =============================================
fan1_input  Pump speed (in rpm)
fan2_input  Fan speed (in rpm)
temp1_input Coolant temperature (in millidegrees Celsius)
=========== =============================================
