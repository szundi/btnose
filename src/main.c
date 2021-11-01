/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#include <device.h>
#include <drivers/sensor.h>
#include <drivers/watchdog.h>
#include <drivers/adc.h>
#include <sys/reboot.h>

#include <logging/log.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define BEACON_INTERVAL_SECONDS 	5
LOG_MODULE_REGISTER(BTNOSE, LOG_LEVEL_DBG);


#pragma pack(1)
static struct s_statedata {
	uint32_t magic;
	uint8_t  version;
	uint8_t  serial;
	uint8_t  temperature_fieldcode;
	int16_t  temperature_value;
	uint8_t  humidity_fieldcode;
	int16_t  humidity_value;
	uint8_t  battery_fieldcode;
	uint8_t  battery_value;
	uint8_t  vcc_fieldcode;
	uint8_t  vcc_value;
} statedata;



/* Set Advertisement data. */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR | BT_LE_AD_GENERAL),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, 
		&statedata, sizeof(statedata)
	)
};
/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)
};




void reboot(const char *reboot_err_message) {
	LOG_ERR("%s - Rebooting...", log_strdup(reboot_err_message));
	k_sleep(K_MSEC(2000));
	sys_reboot(SYS_REBOOT_COLD);
	k_sleep(K_MSEC(1000));
}



static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		reboot("BLE error");
		return;
	} else {
		LOG_DBG("Bluetooth enabled, ready.");
	}
}



static int bt_restart_adv() {
	int err = 0;

	LOG_DBG("Shutdown BLE for restart of advertising... ");
	err = bt_le_adv_stop();
	if (err) {
		LOG_ERR("Advertising failed to stop (err %d)", err);
		reboot("BLE error");
	}

	k_sleep(K_MSEC(200));

	struct bt_le_adv_param no_conn = {
		.id = 0,
		.options = BT_LE_ADV_OPT_USE_IDENTITY, // no connection possible
		.interval_min = BT_GAP_ADV_SLOW_INT_MIN * BEACON_INTERVAL_SECONDS,
		.interval_max = BT_GAP_ADV_SLOW_INT_MAX * BEACON_INTERVAL_SECONDS
	};
	/* Start advertising */
	LOG_DBG("Restarting BLE advertising... ");
	err = bt_le_adv_start(&no_conn,
					ad, ARRAY_SIZE(ad),
			    	sd, ARRAY_SIZE(sd));
					//nullptr, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		reboot("BLE error");
		return err;
	} else {
		LOG_INF("BT adv restarted with serial=%u.", statedata.serial);
		return 0;
	}
}



static void process_sample(const struct device *dev)
{
	static unsigned int obs;
	struct sensor_value temp, hum;
	if (sensor_sample_fetch(dev) < 0) {
		reboot("Sensor sample update error");
		return;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0) {
		reboot("Cannot read temperature channel");
		return;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum) < 0) {
		reboot("Cannot read humidity channel");
		return;
	}

	++obs;
	LOG_DBG("Observation: %u", obs);

	/* display temperature */
	LOG_INF("Temperature: %d.%06d C", temp.val1, temp.val2);
	double temp_tempval = sensor_value_to_double(&temp);
	statedata.temperature_value = (int16_t)(temp_tempval * 100);
	LOG_DBG("statedata.temperature_value: %d", (int16_t)(temp_tempval * 100));

	/* display humidity */
	LOG_INF("Relative Humidity: %d.%06d%%", hum.val1, hum.val2);
	double hum_tempval = sensor_value_to_double(&hum);
	statedata.humidity_value = (int16_t)(hum_tempval * 100);
	LOG_DBG("statedata.humidity_value: %d", statedata.humidity_value);

	statedata.serial++;
	bt_restart_adv();
}




int wdt_channel_id;
const struct device *wdt;
const uint32_t WATCHDOG_PERIOD_MILLISEC = (3600 + 10) * 1000; // reset after dead for 3600 sec

static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
	ARG_UNUSED(wdt_dev);
	ARG_UNUSED(channel_id);
	/* Watchdog timer expired. Handle it here.
	 * Remember that SoC reset will be done soon.
	 */
	LOG_ERR("WDT RESET");
	k_sleep(K_MSEC(500));
	// no reset? do it ourselves.
	sys_reboot(SYS_REBOOT_COLD);
}

static void init_watchdog() {
	struct wdt_timeout_cfg wdt_config;

	#ifdef CONFIG_WDT_0_NAME
	#define WDT_DEV_NAME CONFIG_WDT_0_NAME
	#else
	#define WDT_DEV_NAME DT_WDT_0_NAME
	#endif
	wdt = device_get_binding(DT_LABEL(DT_NODELABEL(wdt)));

	if (!wdt) {
		reboot("Cannot get WDT device");
		return;
	}

	/* Reset SoC when watchdog timer expires. */
	wdt_config.flags = WDT_FLAG_RESET_SOC;

	/* Expire watchdog after window.max milliseconds. */
	wdt_config.window.min = 0;
	wdt_config.window.max = WATCHDOG_PERIOD_MILLISEC; // 11 minutes

	/* Set up watchdog callback. Jump into it when watchdog expired. */
	wdt_config.callback = wdt_callback;

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		reboot("Watchdog install error");
		return;
	}

	int err = wdt_setup(wdt, 0);
	if (err < 0) {
		reboot("Watchdog setup error");
		return;
	} else {
		LOG_INF("Watchdog on.");
	}
}



void main(void)
{	
	LOG_INF("Starting BT Nose Beacon\n");

	init_watchdog();

	const struct device *dev = device_get_binding("SHT3XD");
	if (dev == NULL) {
		reboot("Could not get the sensor device descriptor\n");
		return;
	} else {
		LOG_INF("Sensor found\n");
	}

	/* Initialize the Bluetooth Subsystem */
	int err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		reboot("BT init failed");
	}

	statedata.magic = 0xFEEDBEEF;
	statedata.version = 0x00;
	statedata.serial = 0;
	statedata.temperature_fieldcode = 0x10;
	statedata.temperature_value = 0xFFFF; // 0xFFFF = no data
	statedata.humidity_fieldcode = 0x30; // means 0x18 + 1 - 1 = 1x freq channel
	statedata.humidity_value = 0xFFFF; // 0xFFFF = no data
	statedata.battery_fieldcode = 0xBA; 
	statedata.battery_value = 0xFF; // 0xFF = no data
	statedata.vcc_fieldcode = 0xBB; 
	statedata.vcc_value = 0xFF; // 0xFF = no data*/
	LOG_DBG("BT statedata size == %d", sizeof(statedata));

	uint32_t cycles_max_before_reboot = 10;
	while (true) {
		process_sample(dev);
		k_sleep(K_MSEC(1666));
		LOG_DBG("...");
		k_sleep(K_MSEC(1666));
		LOG_DBG("...");
		k_sleep(K_MSEC(1666));
		LOG_DBG("...");
		wdt_feed(wdt, wdt_channel_id);
		if (--cycles_max_before_reboot == 0) {
			reboot("Uptime too long :) reboot!");
		}
	}
}
