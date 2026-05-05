/*
 * Linux Kernel Driver for the Broadcom APDS-9999 Digital Proximity and RGB Sensor
 *
 * Created by lucx & Lstx
 *
 * The sensor has the I2C ID 0x52
 *
 *
 */

/*
 * --- TODO ---
 *
 * - [ ] power management ?
 * - [ ] of_device_id table for device tree compatibility?
 * - [ ] overflow bits of the measurement registers
 * - [ ] implement triggers
 * - [ ] active_scan_mask
 * - [ ]
 *
 *
 */

// Driver Name - done as define, since we use it multiple times
#define APDS9999_DRIVER_NAME 	"apds9999"

// Register definitions for the device - check datasheet for more information

#define APDS9999_REG_MAIN_CTRL         0x00  /* Main control register */
#define APDS9999_REG_PS_VCSEL          0x01  /* PS VCSEL control register */
#define APDS9999_REG_PS_PULSES         0x02  /* PS pulses control register */
#define APDS9999_REG_PS_MEAS_RATE      0x03  /* PS measurement rate */
#define APDS9999_REG_LS_MEAS_RATE      0x04  /* LS measurement rate */
#define APDS9999_REG_LS_GAIN           0x05  /* LS gain control */
#define APDS9999_REG_PART_ID           0x06  /* Part ID register - R only */
#define APDS9999_REG_MAIN_STATUS       0x07  /* Main status register - R only */
#define APDS9999_REG_PS_DATA_0         0x08  /* PS data low byte - R only */
#define APDS9999_REG_PS_DATA_1         0x09  /* PS data high byte - R only */
#define APDS9999_REG_LS_DATA_IR_0      0x0A  /* IR data low byte - R only */
#define APDS9999_REG_LS_DATA_IR_1      0x0B  /* IR data middle byte - R only */
#define APDS9999_REG_LS_DATA_IR_2      0x0C  /* IR data high byte - R only */
#define APDS9999_REG_LS_DATA_GREEN_0   0x0D  /* Green data low byte - R only */
#define APDS9999_REG_LS_DATA_GREEN_1   0x0E  /* Green data middle byte - R only */
#define APDS9999_REG_LS_DATA_GREEN_2   0x0F  /* Green data high byte - R only */
#define APDS9999_REG_LS_DATA_BLUE_0    0x10  /* Blue data low byte - R only */
#define APDS9999_REG_LS_DATA_BLUE_1    0x11  /* Blue data middle byte - R only */
#define APDS9999_REG_LS_DATA_BLUE_2    0x12  /* Blue data high byte - R only */
#define APDS9999_REG_LS_DATA_RED_0     0x13  /* Red data low byte - R only */
#define APDS9999_REG_LS_DATA_RED_1     0x14  /* Red data middle byte - R only */
#define APDS9999_REG_LS_DATA_RED_2     0x15  /* Red data high byte - R only */
#define APDS9999_REG_INT_CFG           0x19  /* Interrupt configuration */
#define APDS9999_REG_INT_PST           0x1A  /* Interrupt persistence */
#define APDS9999_REG_PS_THRES_UP_0     0x1B  /* PS upper threshold low byte */
#define APDS9999_REG_PS_THRES_UP_1     0x1C  /* PS upper threshold high byte */
#define APDS9999_REG_PS_THRES_LOW_0    0x1D  /* PS lower threshold low byte */
#define APDS9999_REG_PS_THRES_LOW_1    0x1E  /* PS lower threshold high byte */
#define APDS9999_REG_PS_CAN_0          0x1F  /* PS cancellation level low byte */
#define APDS9999_REG_PS_CAN_1          0x20  /* PS cancellation level high byte */
#define APDS9999_REG_LS_THRES_UP_0     0x21  /* LS upper threshold low byte */
#define APDS9999_REG_LS_THRES_UP_1     0x22  /* LS upper threshold middle byte */
#define APDS9999_REG_LS_THRES_UP_2     0x23  /* LS upper threshold high byte */
#define APDS9999_REG_LS_THRES_LOW_0    0x24  /* LS lower threshold low byte */
#define APDS9999_REG_LS_THRES_LOW_1    0x25  /* LS lower threshold middle byte */
#define APDS9999_REG_LS_THRES_LOW_2    0x26  /* LS lower threshold high byte */
#define APDS9999_REG_LS_THRES_VAR      0x27  /* LS variance threshold */

// TODO we may add bitmasks to easily address specific bits in the registers by names

// This sets the endianess by which the iio stores the values in the buffer when scanning the channels
#define APDS9999_CH_ENDIANNESS IIO_CPU

// this defines the attributes for the scan-type used by the intensity related channels
#define APDS9999_INTENSITY_SCAN_TYPE {			\	 /* LS resolution: 13 - 20 bit, default 18 bit ( set in LS_MEAS_RATE ) */
	.sign           = 'u',						\
	.realbits       = 20,						\
	.storagebits    = 32,						\
	.shift          = 0,						\
	.endianness     = APDS9999_CH_ENDIANNESS,	\
}	

// This is the channel definition for the intensity channels - parameters are color and scan index
#define APDS9999_INTENSITY_CHANNEL(_color, _si) { 			\
	.type           = IIO_INTENSITY,						\
	.modified       = 1,									\
	.channel2       = IIO_MOD_LIGHT_##_color,				\
	.address        = APDS9999_REG_LS_DATA_##_color##_0,	\
	.scan_index     = _si,									\
	.scan_type      = APDS9999_INTENSITY_SCAN_TYPE,			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type =	 							\ 	/* the switch case for reading is shared by all channels of the same type - intensity in this case */
		BIT(IIO_CHAN_INFO_RESOLUTION) |						\	/* the resolution can be set in LS_MEAS_RATE */
		BIT(IIO_CHAN_INFO_SAMP_FREQ)  |						\	/* the measurement rate can be set in the LS_MEAS_RATE */ 
		BIT(IIO_CHAN_INFO_SCALE),							\	
}


// This is the type of struct that will eventually hold the data that our driver needs to function
struct apds9999_data {    
	struct i2c_client *client;
	struct iio_dev *indio_dev;
};

// Here we will define all the channels that then get assigned to the iio once created
static const struct iio_chan_spec apds9999_channels[] = {
	// all data registers are locked in hardware if i2c is reading from them. 
	// This is to guarantee to read the same measurement data. Eventual new data is inserted afterwards
	
	
	/* Proximity Sensor (PS) - Maximum resolution 11, default resolution 8 bit ( set in PS_MEAS_RATE ) */
	{
		.type           = IIO_PROXIMITY,
		.address        = APDS9999_REG_PS_DATA_0,
		/* The following two are for buffer reading, so userspace consumes less cpu when reading the whole sensor continiously */
		.scan_index     = 0,							/* This defines the order in which channels are placed inside the buffer */			
		.scan_type      = {
			.sign           = 'u',
			.realbits       = 11,						/* TODO should we modify this based on the value set in PS_MEAS_RATE? */
			.storagebits    = 16,
			.shift          = 0,
			.endianness     = APDS9999_CH_ENDIANNESS,	/* This refers to the buffer used by the driver */
		},
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	
		.info_mask_shared_by_type = 					
			BIT(IIO_CHAN_INFO_RESOLUTION) |				/* the resolution can be set in PS_MEAS_RATE */
			BIT(IIO_CHAN_INFO_SAMP_FREQ),				/* the sampling frequency can be set in the PS_MEAS_RATE */
			/* TODO PS_PULSES and VCSEL options are missing */
	},

	APDS9999_INTENSITY_CHANNEL(RED, 1),
	APDS9999_INTENSITY_CHANNEL(GREEN, 2),
	APDS9999_INTENSITY_CHANNEL(BLUE, 3),
	APDS9999_INTENSITY_CHANNEL(IR, 4),

	/* Ambien Light Sensor (ALS) - This is the same as the green channel, it depends on the configuration */
	{
		.type           = IIO_LIGHT,
		.scan_index     = 5,
		.scan_type      = APDS9999_INTENSITY_SCAN_TYPE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	/* TODO adjust this */
	},

}

static int apds9999_read_raw(){

}

static const struct iio_info apds9999_info = {
	.read_raw = apds9999_read_raw,
	// TODO attributes
	// TODO writes
};

// This function gets called when the kernel loads detects the device and loads this driver. 
// We can save the i2c_client handle for further use
static int apds9999_probe(struct i2c_client *client){
	// data struct that holds the data for out device
	struct apds9999_data *data;
	// the iio_dev that gets returned when we allocate it with the iio subsystem
	struct iio_dev *indio_dev;
	int ret;

	// we use devm_iio over the plain iio, so that the allocated memory gets freed automatically when the driver is detached
	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	// if the allocation failed we got NULL as return value
	if (!indio_dev)
		// this return value means we could not get enough memory
		// we negate it, because the error code in <errno.h> is defined as positive number, to indicate an error we need to return a negative one
		return -ENOMEM;

	
	// set attributes of the iio_dev
	indio_dev->name = APDS9999_DRIVER_NAME;
	indio_dev->info = &apds9999_info;			// hook to the functions to interact with the device
	indio_dev->channels = apds9999_channels;	// the different channels of the device
	indio_dev->num_channels = ARRAY_SIZE(apds9999_channels);
	// These are all the operating modes the device supports
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE | INDIO_EVENT_TRIGGERED; // TODO check if this is actually right
	// TODO do we need available_scan_masks ?

	// TODO do we have to setup the kernel fifo buffer - devm_iio_kfifo_buffer_setup ?

	// retrive the pointer to the area that got allocated at the end of the iio_dev struct by devm_iio_device_alloc
	data = iio_priv(indio_dev);
	// here we save the iio_dev as data into i2c client structure
	i2c_set_clientdata(client, indio_dev);

	// TODO

	// register the driver for this iio device, return if it fails
	ret = devm_iio_device_register(indio_dev);
	if (ret)
		return ret;
	
	println("Hello world from apds9999");
	return 0;
}


// This function is called when the kernel unloads the driver
// We can release the i2c_client handle
static int apds9999_remove(struct i2c_client *client){
	
	// Not sure if we will need this. From a sensor perspective as well as from a kernel one.
	
	// TODO
	println("Goodbye world from apds9999");
	return 0;	
}


// This holds the names of the sensors this driver can handle 
// The second parameter is an optional driver data value that may distinguish different versions of the same sensor
static const struct i2c_device_id apds9999_idtable[] = {
      { "apds9999", 0 },
      { }
};

MODULE_DEVICE_TABLE(i2c, apds9999_idtable);

static struct i2c_driver apds9999_driver = {
      .driver = {
              .name   = APDS9999_DRIVER_NAME,   // this is the drivers name and should match the modules name
      },

      .id_table       = apds9999_idtable,
      .probe          = apds9999_probe,
      .remove         = apds9999_remove,
}

// Device auto detection could be added, but since this is a rather uncommon device, it is advised against in the docs.

module_i2c_driver(apds9999_driver);

MODULE_AUTHOR("Lucx & Lstx");
MODULE_DESCRIPTION("APDS-9999 Digital Proximity and RGB sensor driver");
MODULE_LICENSE("GPL v2");
