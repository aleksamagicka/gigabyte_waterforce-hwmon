// SPDX-License-Identifier: GPL-2.0+
/*
 * hwmon driver for Gigabyte AORUS Waterforce AIO coolers
 *
 * Copyright 2023 Aleksa Savic <savicaleksa83@gmail.com>
 */

#include <linux/debugfs.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#define DRIVER_NAME	"waterforce"

#define USB_VENDOR_ID_GIGABYTE		0x1044
#define USB_PRODUCT_ID_WATERFORCE_1	0x7a4d	/* Gigabyte AORUS WATERFORCE X (240, 280, 360) */
#define USB_PRODUCT_ID_WATERFORCE_2	0x7a52	/* Gigabyte AORUS WATERFORCE X 360G */
#define USB_PRODUCT_ID_WATERFORCE_3	0x7a53	/* Gigabyte AORUS WATERFORCE EX 360 */

#define STATUS_VALIDITY		2	/* seconds */
#define MAX_REPORT_LENGTH	6144

#define FIRMWARE_F14_VER	14
#define MIN_FAN_RPM		750
#define LOWER_MAX_RPM		2800
#define DEFAULT_MAX_RPM		3200

#define WATERFORCE_TEMP_SENSOR	0xD
#define WATERFORCE_FAN_SPEED	0x02
#define WATERFORCE_PUMP_SPEED	0x05
#define WATERFORCE_FAN_DUTY	0x08
#define WATERFORCE_PUMP_DUTY	0x09

DECLARE_COMPLETION(status_report_received);

/* Control commands, inner offsets and lengths */
static const u8 get_status_cmd[] = { 0x99, 0xDA };

#define FIRMWARE_VER_START_OFFSET_1	2
#define FIRMWARE_VER_START_OFFSET_2	3
static const u8 get_firmware_ver_cmd[] = { 0x99, 0xD6 };

/* Offset in below command where CPU temp value should be set */
#define SET_CPU_TEMP_CMD_OFFSET		3
/* Sample command portraying 16c/32t, 5.5GHz CPU */
static const u8 set_cpu_temp_cmd_template[] = { 0x99, 0xE0, 0, 0, 0x20, 0x05, 0x05, 0x10, 0x30 };

/* Offset in below command where channel (pump or fan) should be set and their values */
#define SET_RPM_SPEED_CHANNEL_OFFSET	2
#define SET_RPM_SPEED_CHANNEL_FAN	0x0101
#define SET_RPM_SPEED_CHANNEL_PUMP	0x0402
/* Offsets in below command where RPM should be set */
static const u8 speed_cmd_offsets[] = { 5, 8, 11, 14 };
static const u8 set_rpm_speed_cmd_template[] = {
	0x99, 0xE6, 0, 0, 0, 0, 0, 0x1E, 0, 0, 0x32, 0, 0, 0x41, 0, 0
};

#define GET_STATUS_CMD_LENGTH		2
#define GET_FIRMWARE_VER_CMD_LENGTH	2
#define SET_CPU_TEMP_CMD_LENGTH		9
#define SET_RPM_SPEED_OFFSETS_LENGTH	4
#define SET_RPM_SPEED_CMD_LENGTH	16

static const char *const waterforce_temp_label[] = {
	"Coolant temp",
	"User provided CPU temp"
};

static const char *const waterforce_speed_label[] = {
	"Fan speed",
	"Pump speed"
};

struct waterforce_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct dentry *debugfs;
	struct mutex buffer_lock;	/* For locking access to buffer */
	struct completion fw_version_processed;

	/* Sensor data */
	s32 temp_input[1];
	u16 speed_input[2];	/* Fan and pump speed in RPM */
	u8 duty_input[2];	/* Fan and pump duty in 0-100% */

	u8 *buffer;
	int firmware_version;
	int max_speed_rpm;
	unsigned long updated;	/* jiffies */
};

/*
 * Writes the command to the device with the rest of the report (up to 64 bytes) filled
 * with zeroes
 */
static int waterforce_write_expanded(struct waterforce_data *priv, const u8 *cmd, int cmd_length)
{
	int ret;

	mutex_lock(&priv->buffer_lock);

	memset(priv->buffer, 0x00, MAX_REPORT_LENGTH);
	memcpy(priv->buffer, cmd, cmd_length);
	ret = hid_hw_output_report(priv->hdev, priv->buffer, MAX_REPORT_LENGTH);

	mutex_unlock(&priv->buffer_lock);
	return ret;
}

static int waterforce_get_status(struct waterforce_data *priv)
{
	int ret;

	reinit_completion(&status_report_received);

	/* Send command for getting status */
	ret = waterforce_write_expanded(priv, get_status_cmd, GET_STATUS_CMD_LENGTH);
	if (ret < 0)
		return ret;

	if (!wait_for_completion_timeout
	    (&status_report_received, msecs_to_jiffies(STATUS_VALIDITY * 1000)))
		return -ENODATA;

	return 0;
}

static umode_t waterforce_is_visible(const void *data,
				     enum hwmon_sensor_types type, u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			return 0444;
		case hwmon_temp_input:
			/* Special case to enable writing custom temp value to device, write only */
			if (channel == 1)
				return 0200;
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_label:
		case hwmon_fan_input:
			return 0444;
		case hwmon_fan_target:
			return 0200;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int waterforce_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	int ret;
	struct waterforce_data *priv = dev_get_drvdata(dev);

	if (time_after(jiffies, priv->updated + STATUS_VALIDITY * HZ)) {
		/* Request status on demand */
		ret = waterforce_get_status(priv);
		if (ret < 0)
			return -ENODATA;
	}

	switch (type) {
	case hwmon_temp:
		*val = priv->temp_input[channel];
		break;
	case hwmon_fan:
		*val = priv->speed_input[channel];
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			*val = DIV_ROUND_CLOSEST(priv->duty_input[channel] * 255, 100);
			break;
		default:
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int waterforce_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = waterforce_temp_label[channel];
		break;
	case hwmon_fan:
		*str = waterforce_speed_label[channel];
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int waterforce_get_fw_ver(struct hid_device *hdev)
{
	int ret;
	struct waterforce_data *priv = hid_get_drvdata(hdev);

	ret = waterforce_write_expanded(priv, get_firmware_ver_cmd, GET_FIRMWARE_VER_CMD_LENGTH);
	if (ret < 0)
		return ret;

	if (!wait_for_completion_timeout
	    (&priv->fw_version_processed, msecs_to_jiffies(STATUS_VALIDITY * 1000)))
		return -ENODATA;

	return 0;
}

static int waterforce_set_cpu_temp(struct waterforce_data *priv, long val)
{
	int ret;
	u8 set_cpu_temp_cmd[SET_CPU_TEMP_CMD_LENGTH];

	if (val < 0 || val > 255)
		return -EINVAL;

	memcpy(set_cpu_temp_cmd, set_cpu_temp_cmd_template, SET_CPU_TEMP_CMD_LENGTH);
	set_cpu_temp_cmd[SET_CPU_TEMP_CMD_OFFSET] = val;
	ret = waterforce_write_expanded(priv, set_cpu_temp_cmd, SET_CPU_TEMP_CMD_LENGTH);
	if (ret < 0)
		return ret;

	return 0;
}

static int waterforce_set_fan_speed(struct waterforce_data *priv, int channel, long val)
{
	int i, ret;
	u8 set_rpm_speed_cmd[SET_RPM_SPEED_CMD_LENGTH];

	if (val < MIN_FAN_RPM || val > priv->max_speed_rpm)
		return -EINVAL;

	memcpy(set_rpm_speed_cmd, set_rpm_speed_cmd_template, SET_RPM_SPEED_CMD_LENGTH);
	set_rpm_speed_cmd[SET_RPM_SPEED_CHANNEL_OFFSET+1] =
	    channel == 0 ? SET_RPM_SPEED_CHANNEL_FAN%256 : SET_RPM_SPEED_CHANNEL_PUMP%256;
	set_rpm_speed_cmd[SET_RPM_SPEED_CHANNEL_OFFSET] =
	    channel == 0 ? SET_RPM_SPEED_CHANNEL_FAN/256 : SET_RPM_SPEED_CHANNEL_PUMP/256;

	for (i = 0; i < SET_RPM_SPEED_OFFSETS_LENGTH; i++)
		put_unaligned_be16(val, set_rpm_speed_cmd + speed_cmd_offsets[i]);
	ret = waterforce_write_expanded(priv, set_rpm_speed_cmd, SET_RPM_SPEED_CMD_LENGTH);
	if (ret < 0)
		return ret;

	return 0;
}

static int waterforce_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			    long val)
{
	struct waterforce_data *priv = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return waterforce_set_cpu_temp(priv, val);
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_target:
			return waterforce_set_fan_speed(priv, channel, val);
		default:
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops waterforce_hwmon_ops = {
	.is_visible = waterforce_is_visible,
	.read = waterforce_read,
	.read_string = waterforce_read_string,
	.write = waterforce_write
};

static const struct hwmon_channel_info *waterforce_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_TARGET,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_TARGET),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info waterforce_chip_info = {
	.ops = &waterforce_hwmon_ops,
	.info = waterforce_info,
};

static int waterforce_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data,
				int size)
{
	struct waterforce_data *priv = hid_get_drvdata(hdev);

	if (data[0] == get_firmware_ver_cmd[0] && data[1] == get_firmware_ver_cmd[1]) {
		/* Received a firmware version report */
		priv->firmware_version =
		    data[FIRMWARE_VER_START_OFFSET_1] * 10 + data[FIRMWARE_VER_START_OFFSET_2];
		complete(&priv->fw_version_processed);
		return 0;
	}

	if (data[0] != get_status_cmd[0] || data[1] != get_status_cmd[1]) {
		/* Device returned improper data */
		hid_err_once(priv->hdev, "firmware or device is possibly damaged\n");
		return 0;
	}

	priv->temp_input[0] = data[WATERFORCE_TEMP_SENSOR] * 1000;
	priv->speed_input[0] = get_unaligned_le16(data + WATERFORCE_FAN_SPEED);
	priv->speed_input[1] = get_unaligned_le16(data + WATERFORCE_PUMP_SPEED);
	priv->duty_input[0] = data[WATERFORCE_FAN_DUTY];
	priv->duty_input[1] = data[WATERFORCE_PUMP_DUTY];

	complete(&status_report_received);

	priv->updated = jiffies;

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int firmware_version_show(struct seq_file *seqf, void *unused)
{
	struct waterforce_data *priv = seqf->private;

	seq_printf(seqf, "%u\n", priv->firmware_version);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(firmware_version);

static void waterforce_debugfs_init(struct waterforce_data *priv)
{
	char name[64];

	scnprintf(name, sizeof(name), "%s-%s", DRIVER_NAME, dev_name(&priv->hdev->dev));

	priv->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("firmware_version", 0444, priv->debugfs, priv, &firmware_version_fops);
}

#else

static void waterforce_debugfs_init(struct waterforce_data *priv)
{
}

#endif

static int waterforce_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct waterforce_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);

	/*
	 * Initialize ->updated to STATUS_VALIDITY seconds in the past, making
	 * the initial empty data invalid for waterforce_read without the need for
	 * a special case there.
	 */
	priv->updated = jiffies - STATUS_VALIDITY * HZ;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parse failed with %d\n", ret);
		return ret;
	}

	/*
	 * Enable hidraw so existing user-space tools can continue to work.
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hid hw start failed with %d\n", ret);
		goto fail_and_stop;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "hid hw open failed with %d\n", ret);
		goto fail_and_close;
	}

	priv->buffer = devm_kzalloc(&hdev->dev, MAX_REPORT_LENGTH, GFP_KERNEL);
	if (!priv->buffer) {
		ret = -ENOMEM;
		goto fail_and_close;
	}

	mutex_init(&priv->buffer_lock);
	init_completion(&priv->fw_version_processed);

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "waterforce",
							  priv, &waterforce_chip_info, NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		hid_err(hdev, "hwmon registration failed with %d\n", ret);
		goto fail_and_close;
	}

	hid_device_io_start(hdev);
	ret = waterforce_get_fw_ver(hdev);
	if (ret < 0) {
		hid_err(hdev, "fw version request failed with %d\n", ret);
		goto fail_and_close;
	}
	hid_device_io_stop(hdev);

	if (priv->firmware_version != FIRMWARE_F14_VER &&
	    hdev->product != USB_PRODUCT_ID_WATERFORCE_3)
		priv->max_speed_rpm = LOWER_MAX_RPM;
	else
		priv->max_speed_rpm = DEFAULT_MAX_RPM;

	waterforce_debugfs_init(priv);

	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void waterforce_remove(struct hid_device *hdev)
{
	struct waterforce_data *priv = hid_get_drvdata(hdev);

	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id waterforce_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_1) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_3) },
	{ }
};

MODULE_DEVICE_TABLE(hid, waterforce_table);

static struct hid_driver waterforce_driver = {
	.name = "waterforce",
	.id_table = waterforce_table,
	.probe = waterforce_probe,
	.remove = waterforce_remove,
	.raw_event = waterforce_raw_event,
};

static int __init waterforce_init(void)
{
	return hid_register_driver(&waterforce_driver);
}

static void __exit waterforce_exit(void)
{
	hid_unregister_driver(&waterforce_driver);
}

/*
 * When compiled into the kernel, initialize after the hid bus.
 */
late_initcall(waterforce_init);
module_exit(waterforce_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksa Savic <savicaleksa83@gmail.com>");
MODULE_DESCRIPTION("Hwmon driver for Gigabyte AORUS Waterforce AIO coolers");
