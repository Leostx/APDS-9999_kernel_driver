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
 * - [ ]
 * - [ ]
 * - [ ]
 *
 *
 */


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

// This is the type of struct that will eventually hold the data that our driver needs to function
struct apds9999_data {    
	struct i2c_client *client;
	struct iio_dev *indio_dev; // TODO bohhh
};

static int apds9999_read_register(){

}

static int apds9999_write_register(){

}

// This function gets called when the kernel loads detects the device and loads this driver. 
// We can save the i2c_client handle for further use
static int apds9999_probe(struct i2c_client *client){
	
	// TODO
	
	println("Hello world from apds9999");
}


// This function is called when the kernel unloads the driver
// We can release the i2c_client handle
static int apds9999_remove(struct i2c_client *client){
	
	// Not sure if we will need this. From a sensor perspective as well as from a kernel one.
	
	// TODO
	println("Goodbye world from apds9999");
	
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
              .name   = "apds9999",   // this is the drivers name and should match the modules name
      },

      .id_table       = apds9999_idtable,
      .probe          = apds9999_probe,
      .remove         = apds9999_remove,
}

// Device auto detection could be added, but since this is a rather uncommon device, it is advised against in the docs.

module_i2c_driver(apds9999_driver);

MODULE_AUTHOR("Lucx & Lstx");
MODULE_DESCRIPTION("APDS-9999 Digital Proximity and RGB sensor driver");
MODULE_LICENSE("GPL");
