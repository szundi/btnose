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

#include <logging/log.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

LOG_MODULE_REGISTER(BTNOSE, LOG_LEVEL_DBG);

/*
 * Set Advertisement data. Based on the Eddystone specification:
 * https://github.com/google/eddystone/blob/master/protocol-specification.md
 * https://github.com/google/eddystone/tree/master/eddystone-url
 */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      0xaa, 0xfe, /* Eddystone UUID */
		      0x10, /* Eddystone-URL frame type */
		      0x00, /* Calibrated Tx power at 0m */
		      0x00, /* URL Scheme Prefix http://www. */
		      'z', 'e', 'p', 'h', 'y', 'r',
		      'p', 'r', 'o', 'j', 'e', 'c', 't',
		      0x08) /* .org */
};

/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Beacon started");
}



static void process_sample(struct device *dev)
{
	static unsigned int obs;
	struct sensor_value temp, hum;

	if (sensor_sample_fetch(dev) < 0) {
		LOG_ERR("Sensor sample update error");
		return;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0) {
		LOG_ERR("Cannot read HTS221 temperature channel");
		return;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum) < 0) {
		LOG_ERR("Cannot read HTS221 humidity channel");
		return;
	}

	++obs;

#if 0
	LOG_INF("Observation:%u", obs);

	/* display temperature */
	LOG_INF("Temperature:%.1f C", sensor_value_to_double(&temp));

	/* display humidity */
	LOG_INF("Relative Humidity:%.1f%%", sensor_value_to_double(&hum));
#else
	LOG_INF("Observation:%u: Temp(%d.%01uC), rH(%d%%)", 
		obs, temp.val1, temp.val2/100000, hum.val1);
#endif

}



void main(void)
{
	LOG_INF("Starting Beacon Demo");

	/* 
	 *  see ./build/zephyr/include/generated/generated_dts_board.conf
	 */
	struct device *dev = device_get_binding(DT_INST_0_ST_HTS221_LABEL);
	if (dev == NULL) {
		LOG_ERR("Could not get HTS221 device: %s", DT_INST_0_ST_HTS221_LABEL);
		return;
	} else {
		LOG_INF("HTS221 found");
	}

#if 0
	/* Initialize the Bluetooth Subsystem */
	int err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
	}
#else
	(void) bt_ready;
#endif

	while (!IS_ENABLED(CONFIG_HTS221_TRIGGER)) {
		process_sample(dev);
		k_sleep(K_MSEC(2000));
		//LOG_INF("...");
	}
}
