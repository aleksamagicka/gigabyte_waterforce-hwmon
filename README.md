# gigabyte_waterforce-hwmon

_Hwmon Linux kernel driver for monitoring Gigabyte AORUS Waterforce AIO coolers_

## Overview

The following devices are supported by this driver:

* Gigabyte AORUS WATERFORCE X (240, 280, 360)
* Gigabyte AORUS WATERFORCE X 360G
* Gigabyte AORUS WATERFORCE EX 360

Being a standard `hwmon` driver, it provides readings via `sysfs`, which are easily accessible through `lm-sensors` as usual.

Report offsets were taken from https://github.com/namidairo/liquidctl/commit/a56db61350d01db8c3a008bb8816d870f6f2350d.

## Installation and usage

**This driver is WIP - missing and/or possibly buggy functionality or behaviour can be expected at this stage.**

First, clone the repository by running:

```commandline
git clone https://github.com/aleksamagicka/waterforce-hwmon.git
```

Then, compile it and insert it into the running kernel, replacing the existing instance (if needed):

```commandline
make dev
```

You can then try running `sensors` and your devices should be listed there.
