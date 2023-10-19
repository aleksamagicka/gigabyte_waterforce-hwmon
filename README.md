# gigabyte_waterforce-hwmon

_Hwmon Linux kernel driver for monitoring Gigabyte AORUS Waterforce AIO coolers_

## Overview

The following devices are supported by this driver:

* Gigabyte AORUS WATERFORCE X240
* Gigabyte AORUS WATERFORCE X280
* Gigabyte AORUS WATERFORCE X360

Being a standard `hwmon` driver, it provides readings via `sysfs`, which are easily accessible through `lm-sensors` as usual.

Report offsets were initially taken from [here](https://github.com/namidairo/liquidctl/commit/a56db61350d01db8c3a008bb8816d870f6f2350d)
and confirmed by me when I acquired a X240.

## Installation and usage

First, clone the repository by running:

```commandline
git clone https://github.com/aleksamagicka/waterforce-hwmon.git
```

Then, compile it and insert it into the running kernel, replacing the existing instance (if needed):

```commandline
make dev
```

You can then try running `sensors` and your device(s) should be listed there.
