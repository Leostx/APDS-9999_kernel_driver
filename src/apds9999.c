/*
 * Linux Kernel Driver for the Broadcom APDS-9999 Digital Proximity and RGB Sensor
 *
 * Created by lucx & Lstx
 *
 * The sensor has the I2C ID 0x52
 *
 * Terminology:
 * 	PS: Proximity Sensor
 * 	LS: Light Sensor
 * 	ALS: Ambient Light Sensor
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
 * - [ ] PS_DATA = PS_MEAS – PS_CAN
 * - [ ] thresholds
 * - [ ] ps scale
 * - [ ] use regfield for writing single bits
 * - [ ]
 * - [ ]
 *
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>     // FIELD_PREP & FIELD_GET
#include <linux/bits.h>         // BIT & GENMASK
#include <linux/device.h>       // dev_err, dev_info, dev_warn
#include <linux/errno.h>        // error codes like -EINVAL, -ENODEV
#include <linux/types.h>        // types like __le16
//#include <linux/byteorder.h>    // byte order macros like le16_to_cpu
#include <linux/property.h>     // may be needed later for device tree support // TODO

/* ------------------- HELPER MAKROS ------------------- */
// Driver Name - done as define, since we use it multiple times
#define APDS9999_DRIVER_NAME 	"apds9999"

// We define this macro to shift the default value into the correct bitfield position
// This is done to avoid some nested BUILD_BUG_ON_ZERO makro expansions, which would place a struct inside a sizeof()
// since we just prep the default values from the datasheet, this is save if we check while testing
#define PREP_DEF(_mask, _val) (((_val) << __bf_shf(_mask)) & (_mask))




/* ------------------- REGISTER DEFINES ------------------- */

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


/* Following are the bitmasks for certain registers, since different bits have different functionality */

// The following bits are the ones inside the MAIN_CTRL
#define APDS9999_CTRL_SAI_PS     	BIT(6)  /* sleep after interrupt for PS */
#define APDS9999_CTRL_SAI_LS     	BIT(5)  /* sleep after interrupt for LS */
#define APDS9999_CTRL_SW_RESET     	BIT(4)  /* software reset */
#define APDS9999_CTRL_RGB_MODE      BIT(2)  /* 0 = ALS and IR are active. 1 = RGB and IR are active */
#define APDS9999_CTRL_LS_EN         BIT(1)  /* light sensor enable */
#define APDS9999_CTRL_PS_EN         BIT(0)  /* proximity enable */

// The following are the bits in PS_VCSEL - writing them restarts the PS state machine // TODO is this relevant?
#define APDS9999_PS_VCSEL_FREQ      GENMASK(6, 4)
#define APDS9999_PS_VCSEL_CURR      GENMASK(2, 0)

/* Possible VCSEL Frequency values  */
#define APDS9999_PS_VCSEL_FREQ_60kHz 	0b011	/* default */
#define APDS9999_PS_VCSEL_FREQ_70kHz 	0b100
#define APDS9999_PS_VCSEL_FREQ_80kHz 	0b101
#define APDS9999_PS_VCSEL_FREQ_90kHz 	0b110
#define APDS9999_PS_VCSEL_FREQ_100kHz 	0b111

/* Possible VCSEL Current values  */
#define APDS9999_PS_VCSEL_CURR_DEF		0b110 	/* default*/
#define APDS9999_PS_VCSEL_CURR_10mA		0b010
#define APDS9999_PS_VCSEL_CURR_25mA		0b011

/*
 * From the datasheet for PS_MEAS_RATE and LS_MEAS_RATE:
 * 	When the measurement repeat rate is programmed to be faster than possible for the programmed ADC measurement time,
 * 	the repeat rate will be lower than programmed (maximum speed).
 *
 * 	Writing to this register stops the ongoing measurements and starts new measurements (depending on the respective enable bits).
 *
 */

// The following are the bits in PS_MEAS_RATE
#define APDS9999_PS_RESO      		GENMASK(4, 3)	/* Proximity Sensor resolution (bits) */
#define APDS9999_PS_RATE      		GENMASK(2, 0)	/* Proximity Sensor measurement rate (ms) - controls the timing of the periodic measurements of the PS in active mode*/

/* Possible PS Resolution values  */
#define APDS9999_PS_RESO_8_BIT		0b00	/* default*/
#define APDS9999_PS_RESO_9_BIT		0b01
#define APDS9999_PS_RESO_10_BIT		0b10
#define APDS9999_PS_RESO_11_BIT		0b11

/* Possible PS Measurement rate values  */
#define APDS9999_PS_RATE_6_25_MS	0b001
#define APDS9999_PS_RATE_12_5_MS	0b010
#define APDS9999_PS_RATE_25_MS      0b011
#define APDS9999_PS_RATE_50_MS      0b100
#define APDS9999_PS_RATE_100_MS     0b101	/* default */
#define APDS9999_PS_RATE_200_MS     0b110
#define APDS9999_PS_RATE_400_MS     0b111

// The following are the bits in LS_MEAS_RATE
#define APDS9999_LS_RESO      		GENMASK(6, 4)	/* Light Sensor resolution (ms) */
#define APDS9999_LS_RATE      		GENMASK(2, 0) 	/* Light Sensor measurement rate (ms)*/

/* Possible LS Resolution values  */
#define APDS9999_LS_RESO_20_BIT_400_MS    0b000
#define APDS9999_LS_RESO_19_BIT_200_MS    0b001
#define APDS9999_LS_RESO_18_BIT_100_MS    0b010	/* default */
#define APDS9999_LS_RESO_17_BIT_50_MS     0b011
#define APDS9999_LS_RESO_16_BIT_25_MS     0b100
#define APDS9999_LS_RESO_13_BIT_3_125_MS  0b101

/* Possible LS Measurement rate values  */
#define APDS9999_LS_RATE_25_MS     0b000
#define APDS9999_LS_RATE_50_MS     0b001
#define APDS9999_LS_RATE_100_MS    0b010	/* default */
#define APDS9999_LS_RATE_200_MS    0b011
#define APDS9999_LS_RATE_500_MS    0b100
#define APDS9999_LS_RATE_1000_MS   0b101
#define APDS9999_LS_RATE_2000_MS   0b110

// The following are the bits in the LS_GAIN
#define APDS9999_LS_GAIN_RANGE      GENMASK(2, 0) 	/* Writing to this register resets the LS state machine and starts new measurements */

/* Possible LS Gain values */
#define APDS9999_LS_GAIN_RANGE_1      0b000
#define APDS9999_LS_GAIN_RANGE_3      0b001	/* default */
#define APDS9999_LS_GAIN_RANGE_6      0b010
#define APDS9999_LS_GAIN_RANGE_9      0b011
#define APDS9999_LS_GAIN_RANGE_18     0b100

// The following are the bits in the PART_ID
#define APDS9999_ID_PART	   		GENMASK(7, 4)	/* Part number id */
#define APDS9999_ID_REVI	   		GENMASK(3, 0)	/* revision id */

// The following are the bits in the MAIN_STATUS
#define APDS9999_STATUS_POS         BIT(5)	/* Power On Status*/
#define APDS9999_STATUS_LS_INT      BIT(4)	/* interrupt occured for ls*/
#define APDS9999_STATUS_LS_DATA 	BIT(3)	/* new ls data is ready*/
#define APDS9999_STATUS_PS_INT      BIT(1)	/* interrupt occured for ps*/
#define APDS9999_STATUS_PS_DATA 	BIT(0)	/* new ps data is ready*/


// The following are the special bits for PS_DATA - regards PS_DATA_1
#define APDS9999_REG_PS_DATA_1_OVRFLW	BIT(3)	/* does the measurement lie outside of the measurable range */

// The following are the bits for the INT_CFG
#define APDS9999_INT_CFG_LS_INT_SEL			 GENMASK(5, 4)
#define APDS9999_INT_CFG_LS_VAR_MODE         BIT(3)  /* LS interrupt mode: 0=threshold, 1=variation */
#define APDS9999_INT_CFG_LS_INT_EN           BIT(2)  /* LS interrupt enabled */
#define APDS9999_INT_CFG_PS_LOGIC_MODE       BIT(1)  /* 0=INT signal is active until status is cleared, 1=INT updated after every measurement */
#define APDS9999_INT_CFG_PS_INT_EN           BIT(0)  /* PS interrupt enabled */

/* Possible INT_CFG LS_INT_SEL values */
#define APDS9999_INT_CFG_LS_INT_SEL_IR			0b00
#define APDS9999_INT_CFG_LS_INT_SEL_GREEN_ALS	0b01	/* default */
#define APDS9999_INT_CFG_LS_INT_SEL_RED			0b10
#define APDS9999_INT_CFG_LS_INT_SEL_BLUE		0b11


// The following are the bits for the INT_PST
/* sets the number of similar consecutive ints, before the int is asserted */
#define APDS9999_INT_PST_LS_PERS			 GENMASK(7, 4)
#define APDS9999_INT_PST_PS_PERS			 GENMASK(3, 0)

// TODO thresholds etc

/* DEFINE DEFAULT VALUES - taken from the datasheet */
// PREP_DEF is used to fill just a subset of bits.

#define APDS9999_REG_MAIN_CTRL_DEF              0x00 											/* 0x00 */
#define APDS9999_REG_PS_VCSEL_DEF																/* 0x36 */ \
			PREP_DEF(APDS9999_PS_VCSEL_FREQ, APDS9999_PS_VCSEL_FREQ_60kHz) | \
			PREP_DEF(APDS9999_PS_VCSEL_CURR, APDS9999_PS_VCSEL_CURR_DEF)
#define APDS9999_REG_PS_PULSES_DEF				0x08 											/* 0x08 */
#define APDS9999_REG_PS_MEAS_RATE_DEF 															/* 0x05 */ \
		PREP_DEF(APDS9999_PS_RESO, APDS9999_PS_RESO_8_BIT) | \
		PREP_DEF(APDS9999_PS_RATE, APDS9999_PS_RATE_100_MS)
#define APDS9999_REG_LS_MEAS_RATE_DEF															/* 0x22 */ \
		PREP_DEF(APDS9999_LS_RESO, APDS9999_LS_RESO_18_BIT_100_MS) | \
		PREP_DEF(APDS9999_LS_RATE, APDS9999_LS_RATE_100_MS)
#define APDS9999_REG_LS_GAIN_DEF																/* 0x01 */ \
			PREP_DEF(APDS9999_LS_GAIN_RANGE, APDS9999_LS_GAIN_RANGE_3)
#define APDS9999_REG_PART_ID_DEF				0xC2 											/* 0xc2 */
#define APDS9999_REG_MAIN_STATUS_DEF															/* 0x20 */ \
			PREP_DEF(APDS9999_STATUS_POS, 1)
#define APDS9999_REG_PS_DATA_0_DEF				0x00 											/* 0x00 */
#define APDS9999_REG_PS_DATA_1_DEF              0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_IR_0_DEF           0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_IR_1_DEF           0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_IR_2_DEF           0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_GREEN_0_DEF        0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_GREEN_1_DEF        0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_GREEN_2_DEF        0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_BLUE_0_DEF         0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_BLUE_1_DEF         0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_BLUE_2_DEF         0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_RED_0_DEF          0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_RED_1_DEF          0x00 											/* 0x00 */
#define APDS9999_REG_LS_DATA_RED_2_DEF          0x00 											/* 0x00 */
#define APDS9999_REG_INT_CFG_DEF																/* 0x10 */ \
			PREP_DEF(APDS9999_INT_CFG_LS_INT_SEL, APDS9999_INT_CFG_LS_INT_SEL_GREEN_ALS)
#define APDS9999_REG_INT_PST_DEF          		0x00 											/* 0x00 */
#define APDS9999_REG_PS_THRES_UP_0_DEF			0xFF											/* 0xff */
#define APDS9999_REG_PS_THRES_UP_1_DEF			0x07											/* 0x07 */
#define APDS9999_REG_PS_THRES_LOW_0_DEF			0x00 											/* 0x00 */
#define APDS9999_REG_PS_THRES_LOW_1_DEF			0x00 											/* 0x00 */
#define APDS9999_REG_PS_CAN_0_DEF				0x00 											/* 0x00 */
#define APDS9999_REG_PS_CAN_1_DEF				0x00 											/* 0x00 */
#define APDS9999_REG_LS_THRES_UP_0_DEF			0xFF											/* 0xff */
#define APDS9999_REG_LS_THRES_UP_1_DEF			0xFF											/* 0xff */
#define APDS9999_REG_LS_THRES_UP_2_DEF			0x0F											/* 0x0f */
#define APDS9999_REG_LS_THRES_LOW_0_DEF			0x00 											/* 0x00 */
#define APDS9999_REG_LS_THRES_LOW_1_DEF			0x00 											/* 0x00 */
#define APDS9999_REG_LS_THRES_LOW_2_DEF			0x00 											/* 0x00 */
#define APDS9999_REG_LS_THRES_VAR_DEF			0x00 											/* 0x00 */


/* ------------------- END REGISTER DEFINES ------------------- */


/* ------------------- IIO CHANNEL DEFINES ------------------- */

// This sets the endianess by which the iio stores the values in the buffer when scanning the channels
#define APDS9999_CH_ENDIANNESS IIO_CPU

// this defines the attributes for the scan-type used by the intensity related channels
#define APDS9999_INTENSITY_SCAN_TYPE {				 /* LS resolution: 13 - 20 bit, default 18 bit ( set in LS_MEAS_RATE ) */ \
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
	.info_mask_shared_by_type =	 							/* the switch case for reading is shared by all channels of the same type - intensity in this case */ \
		BIT(IIO_CHAN_INFO_RESOLUTION) |						/* the resolution can be set in LS_MEAS_RATE */ \
		BIT(IIO_CHAN_INFO_SAMP_FREQ)  |						/* the measurement rate can be set in the LS_MEAS_RATE */ \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN) |					/* the gain can be set in LS_GAIN */ \
		BIT(IIO_CHAN_INFO_PROCESSED),                       \
}

/* ------------------- END IIO CHANNEL DEFINES ------------------- */

// This is the type of struct that will eventually hold the data that our driver needs to function
struct apds9999_data {
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
};

// This table is for converting the light sensor readings to lux values - this comes from the datasheet
// Resolution (lux/count) indexed by [gain][resolution]
// Gain indices: 0=1x, 1=3x, 2=6x, 3=9x, 4=18x
// Resolution indices: 0=20bit, 1=19bit, 2=18bit, 3=17bit, 4=16bit
static const float ls_lux_conversion_map[5][5] = {
    { 0.136, 0.273, 0.548, 1.099, 2.193 }, 			/* 1x   */
    { 0.045, 0.090, 0.180, 0.359, 0.722 }, 			/* 3x   */
    { 0.022, 0.045, 0.090, 0.179, 0.360 }, 			/* 6x   */
    { 0.015, 0.030, 0.059, 0.119, 0.239 }, 			/* 9x   */
    { 0.007, 0.015, 0.029, 0.059, 0.117 }, 			/* 18x  */
};

/* ------------------- REGMAP CONFIG ------------------- */

/* Explenation in the apds9999_regmap_config struct at the end of the section */

static const struct regmap_range apds9999_readable_ranges[] = {
	regmap_reg_range(APDS9999_REG_MAIN_CTRL, APDS9999_REG_LS_THRES_VAR),
};

static const struct regmap_access_table apds9999_readable_table = {
	.yes_ranges	= apds9999_readable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(apds9999_readable_ranges),
};

static const struct regmap_range apds9999_writeable_ranges[] = {
	regmap_reg_range(APDS9999_REG_MAIN_CTRL, APDS9999_REG_LS_GAIN),
	regmap_reg_range(APDS9999_REG_INT_CFG, APDS9999_REG_LS_THRES_VAR),
};

static const struct regmap_access_table apds9999_writeable_table = {
	.yes_ranges	= apds9999_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(apds9999_writeable_ranges),
};

// Volatile registers are those that can change without the driver explicitly writing to them
static const struct regmap_range apds9999_volatile_ranges[] = {
	regmap_reg_range(APDS9999_REG_MAIN_STATUS, APDS9999_REG_MAIN_STATUS), /* main status chanegs on int or data ready */
	regmap_reg_range(APDS9999_REG_PS_DATA_0, APDS9999_REG_LS_DATA_RED_2), /* all data registers are volatile for shure */
};

static const struct regmap_access_table apds9999_volatile_table = {
	.yes_ranges	= apds9999_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(apds9999_volatile_ranges),
};

// Precious registers are those that can alter hardware states when read
static const struct regmap_range apds9999_precious_ranges[] = {
	/*
	*	APDS9999_REG_MAIN_CTRL:
	*		when SAI_LS or SAI_PS are set, the LS_EN or PS_EN are cleared when this register is read
	*/
	regmap_reg_range(APDS9999_REG_MAIN_CTRL, APDS9999_REG_MAIN_CTRL),
};

static const struct regmap_access_table apds9999_precious_table = {
	.yes_ranges	= apds9999_precious_ranges,
	.n_yes_ranges	= ARRAY_SIZE(apds9999_precious_ranges),
};


// Here we set the default register values. By doing this we should reduce the startup time since the data does not need to get read at startup
static const struct reg_default apds9999_reg_defaults[] = {

	{ APDS9999_REG_MAIN_CTRL, APDS9999_REG_MAIN_CTRL_DEF },
	{ APDS9999_REG_PS_VCSEL, APDS9999_REG_PS_VCSEL_DEF },
	{ APDS9999_REG_PS_PULSES, APDS9999_REG_PS_PULSES_DEF },
	{ APDS9999_REG_PS_MEAS_RATE, APDS9999_REG_PS_MEAS_RATE_DEF },
	{ APDS9999_REG_LS_MEAS_RATE, APDS9999_REG_LS_MEAS_RATE_DEF },
	{ APDS9999_REG_LS_GAIN, APDS9999_REG_LS_GAIN_DEF },
	{ APDS9999_REG_PART_ID, APDS9999_REG_PART_ID_DEF },
	{ APDS9999_REG_MAIN_STATUS, APDS9999_REG_MAIN_STATUS_DEF },
	{ APDS9999_REG_PS_DATA_0, APDS9999_REG_PS_DATA_0_DEF },
	{ APDS9999_REG_PS_DATA_1, APDS9999_REG_PS_DATA_1_DEF },
	{ APDS9999_REG_LS_DATA_IR_0, APDS9999_REG_LS_DATA_IR_0_DEF },
	{ APDS9999_REG_LS_DATA_IR_1, APDS9999_REG_LS_DATA_IR_1_DEF },
	{ APDS9999_REG_LS_DATA_IR_2, APDS9999_REG_LS_DATA_IR_2_DEF },
	{ APDS9999_REG_LS_DATA_GREEN_0, APDS9999_REG_LS_DATA_GREEN_0_DEF },
	{ APDS9999_REG_LS_DATA_GREEN_1, APDS9999_REG_LS_DATA_GREEN_1_DEF },
	{ APDS9999_REG_LS_DATA_GREEN_2, APDS9999_REG_LS_DATA_GREEN_2_DEF },
	{ APDS9999_REG_LS_DATA_BLUE_0, APDS9999_REG_LS_DATA_BLUE_0_DEF },
	{ APDS9999_REG_LS_DATA_BLUE_1, APDS9999_REG_LS_DATA_BLUE_1_DEF },
	{ APDS9999_REG_LS_DATA_BLUE_2, APDS9999_REG_LS_DATA_BLUE_2_DEF },
	{ APDS9999_REG_LS_DATA_RED_0, APDS9999_REG_LS_DATA_RED_0_DEF },
	{ APDS9999_REG_LS_DATA_RED_1, APDS9999_REG_LS_DATA_RED_1_DEF },
	{ APDS9999_REG_LS_DATA_RED_2, APDS9999_REG_LS_DATA_RED_2_DEF },
	{ APDS9999_REG_INT_CFG, APDS9999_REG_INT_CFG_DEF },
	{ APDS9999_REG_INT_PST, APDS9999_REG_INT_PST_DEF },
	{ APDS9999_REG_PS_THRES_UP_0, APDS9999_REG_PS_THRES_UP_0_DEF },
	{ APDS9999_REG_PS_THRES_UP_1, APDS9999_REG_PS_THRES_UP_1_DEF },
	{ APDS9999_REG_PS_THRES_LOW_0, APDS9999_REG_PS_THRES_LOW_0_DEF },
	{ APDS9999_REG_PS_THRES_LOW_1, APDS9999_REG_PS_THRES_LOW_1_DEF },
	{ APDS9999_REG_PS_CAN_0, APDS9999_REG_PS_CAN_0_DEF },
	{ APDS9999_REG_PS_CAN_1, APDS9999_REG_PS_CAN_1_DEF },
	{ APDS9999_REG_LS_THRES_UP_0, APDS9999_REG_LS_THRES_UP_0_DEF },
	{ APDS9999_REG_LS_THRES_UP_1, APDS9999_REG_LS_THRES_UP_1_DEF },
	{ APDS9999_REG_LS_THRES_UP_2, APDS9999_REG_LS_THRES_UP_2_DEF },
	{ APDS9999_REG_LS_THRES_LOW_0, APDS9999_REG_LS_THRES_LOW_0_DEF },
	{ APDS9999_REG_LS_THRES_LOW_1, APDS9999_REG_LS_THRES_LOW_1_DEF },
	{ APDS9999_REG_LS_THRES_LOW_2, APDS9999_REG_LS_THRES_LOW_2_DEF },
	{ APDS9999_REG_LS_THRES_VAR, APDS9999_REG_LS_THRES_VAR_DEF },

};

// This is the config structure for the regmap interface, that enables us to easily read and write registers
static const struct regmap_config apds9999_regmap_config = {
	.name = "apds9999_regmap",	/* Name, not mandatory, only if we should have multiple regmaps */
	.reg_bits = 8,				/* Number of bits to address a register - register addresses are 1-byte alligned */
	.val_bits = 8,				/* Number of bits inside a register */

	.rd_table = &apds9999_readable_table,		/* This defines the range of registers that are readable (all) */
	.wr_table = &apds9999_writeable_table,		/* This defines the two ranges of registers that are writable */

    .volatile_table = &volatile_range_cfg,		/* These registers change on hardware events */
    .precious_table = &precious_range_cfg,		/* These registers change hardware on reads */

	.reg_defaults = apds9999_reg_defaults,		/* default values of the registers */
    .num_reg_defaults = ARRAY_SIZE(apds9999_reg_defaults),

	.max_register = APDS9999_REG_LS_THRES_VAR,
	.cache_type = REGCACHE_MAPLE,				/* this is the type of cache. Seems the best tradeoff */

	//TODO maybe wee want to add ranges for the consecutive reads
};

/* ------------------- END REGMAP CONFIG ------------------- */

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
			BIT(IIO_CHAN_INFO_SAMP_FREQ)  |				/* the sampling frequency can be set in the PS_MEAS_RATE */
			BIT(IIO_CHAN_INFO_OFFSET),					/* This is for PS_CAN */
			/* TODO PS_PULSES and PS_VCSEL options are missing */
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

// this function reads the raw value from the proximity sensor into val
static int apds9999_read_ps_raw(struct apds9999_data *data, unsigned int address, int *val){
	// variable to hold the resolution setting
	unsigned int setting;
	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	// regmap_reads takes the regmap, the register and a pointer to store the value there
	ret = regmap_read(data->regmap, APDS9999_REG_PS_MEAS_RATE, *setting);
	// if regmap reading the settings failed, return early with the error code
	if(ret){
		dev_err(indio_dev, "regmap reading ps resolution failed.\n");
		return ret;
	}

	// extract the resolution fields from the read register
	setting = FIELD_GET(APDS9999_PS_RESO, setting);

	// if the resolution is smaller or eqaul to n-bits, read a single register, otherwise read both
	if(setting <= APDS9999_PS_RESO_8_BIT){
		ret = regmap_read(data->regmap, address, val);
	}else {
		// little endian 16-bit value is to buffer our 2-register read
		__le16 regs;
		// regmap bulk read takes the number of bytes to read as the last argument
		ret = regmap_bulk_read(data->regmap, address, &regs, 2);
		// convert the final value to cpu endianness and save it in val
		*val = le16_to_cpu(regs);
	}

	// if ret is 0, everything went fine. Inform the caller that we read an int
	if (!ret)
		ret = IIO_VAL_INT;

	return ret;
}


// this function reads the raw value from the light sensor into val
static int apds9999_read_ls_raw(struct apds9999_data *data, unsigned int address, int *val){
	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	// TODO check if this works also on lower resolution settings or if we get spurios MSBs

	// little endian 32-bit value is to buffer our 3-register read
	__le32 buf;
	// regmap bulk read takes the number of bytes to read as the last argument
	ret = regmap_bulk_read(data->regmap, chan->address, &buf, 3);
	// convert the final value to cpu endianness and save it in val
	*val = le32_to_cpu(regs);


	// if ret is 0, everything went fine. Inform the caller that we read an int
	if (!ret)
		ret = IIO_VAL_INT;

	return ret;
}

// This callback gets executed when reading raw or scale from sysfs
/*
 * indio_dev: 		iio device
 * iio_chan_spec: 	specs for the channel that is beeing read
 * val:			primary read value
 * val2:		secondary read value
 * mask:		bitmask, which type of value is beeing requested
*/
static int apds9999_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask){
	// retrive the pointer to the data that is associated with our iio device
	struct apds9999_data *data = iio_priv(indio_dev);


	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	switch (mask) {
		case IIO_CHAN_INFO_RAW:
			switch (chan->type) {
				case IIO_PROXIMITY:
					ret = apds9999_read_ps_raw(data, chan->address, val);
					break;
				case IIO_INTENSITY:
					ret = apds9999_read_ls_raw(data, chan->address, val);
					break;
				default:
					ret = -EINVAL;
			}
			break;
		case IIO_CHAN_INFO_PROCESSED:
			switch (chan->type) {
				case IIO_INTENSITY:
					// variable to hold the resolution setting
					unsigned int reso;
					// variable to hold the gain setting
					unsigned int gain;


					// regmap_reads takes the regmap, the register and a pointer to store the value there
					ret = regmap_read(data->regmap, APDS9999_REG_LS_MEAS_RATE, *reso);
					// if regmap reading the settings failed, return early with the error code
					if(ret){
						dev_err(indio_dev, "regmap reading ls resolution failed.\n");
						return ret;
					}
					// regmap_reads takes the regmap, the register and a pointer to store the value there
					ret = regmap_read(data->regmap, APDS9999_REG_LS_GAIN, *gain);
					// if regmap reading the settings failed, return early with the error code
					if(ret){
						dev_err(indio_dev, "regmap reading ls gain failed.\n");
						return ret;
					}

					// extract the resolution fields from the read register
					reso = FIELD_GET(APDS9999_LS_RESO, reso);
					// extract the resolution fields from the read register
					gain = FIELD_GET(APDS9999_LS_GAIN_RANGE, gain);

					if(reso == APDS9999_LS_RESO_13_BIT_3_125_MS){
						dev_err(indio_dev, "13-bit ls resolution has no scaling factor. \n");
						return ret;
					}

					// here we read the raw ls value into val
					ret = apds9999_read_ls_raw(data, chan->address, val);
					// here we scale the val by a constant we retrieve from the map based on gain and resolution
					*val = &val * ls_lux_conversion_map[gain][reso];
			}
			break;

		// TODO other cases such as scale etc
	}

	return ret;
}

static int apds9999_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int val, int val2, long mask){
	// retrive the pointer to the data that is associated with our iio device
	struct apds9999_data *data = iio_priv(indio_dev);

	// this is the value that gets actually written to the register
	int to_write;

	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	switch (mask) {
		case IIO_CHAN_INFO_RESOLUTION:
			switch (chan->type) {
				case IIO_PROXIMITY:
					if (val < 8 || val > 11){
						dev_err(indio_dev, "proximity sensor resolution should be between 8 and 11 bit.\n");
						return ret;
					}

					// from the datasheet we get, that we just have option 0-3
					to_write = val - 8;
					// this performs a read, modify, write cycle
					/* regmap, register to write, mask to use, what to write */
					ret = regmap_update_bits(data->regmap, APDS9999_REG_PS_MEAS_RATE, APDS9999_PS_RESO, to_write);
					if(ret){
						dev_err(indio_dev, "failed updating proximity sensor resolution.\n");
						return ret;
					}

					break;
				case IIO_INTENSITY:
					if (val < 13 || val > 20){
						dev_err(indio_dev, "light sensor resolution should be between 13 and 20 bit.\n");
						return ret;
					}

					// from the datasheet we get, that we just have option 0-5. They are linera, with a jump. Option with id 5 is actually 13 bit
					if(val == 13){
						to_write = 5
					}else{
						to_write = 20 - val;
					}
					// this performs a read, modify, write cycle
					/* regmap, register to write, mask to use, what to write */
					ret = regmap_update_bits(data->regmap, APDS9999_REG_LS_MEAS_RATE, APDS9999_LS_RESO, to_write);
					if(ret){
						dev_err(indio_dev, "failed updating light sensor resolution.\n");
						return ret;
					}

					break;
				default:
					return -EINVAL;
			}
			break;
		case IIO_CHAN_INFO_SAMP_FREQ:
			switch (chan->type) {
				case IIO_PROXIMITY:
					if(val > 200){
						to_write = APDS9999_PS_RATE_400_MS;
					}else if(val > 100){
						to_write = APDS9999_PS_RATE_200_MS;
					}else if(val > 50){
						to_write = APDS9999_PS_RATE_100_MS;
					}else if(val > 25){
						to_write = APDS9999_PS_RATE_250_MS;
					}else if(val > 12){
						to_write = APDS9999_PS_RATE_50_MS;
					}else if(val > 6){
						to_write = APDS9999_PS_RATE_12_5_MS;
					}else{
						to_write = APDS9999_PS_RATE_6_25_MS;
					}
					// TODO may add some debug info

					// this performs a read, modify, write cycle
					/* regmap, register to write, mask to use, what to write */
					ret = regmap_update_bits(data->regmap, APDS9999_REG_PS_MEAS_RATE, APDS9999_PS_RATE, to_write);
					if(ret){
						dev_err(indio_dev, "failed updating proximity sensor measurment rate.\n");
						return ret;
					}

					break;
				case IIO_INTENSITY:
					if(val > 1000){
						to_write = APDS9999_LS_RATE_2000_MS;
					}else if(val > 500){
						to_write = APDS9999_LS_RATE_1000_MS;
					}else if(val > 200){
						to_write = APDS9999_LS_RATE_500_MS;
					}else if(val > 100){
						to_write = APDS9999_LS_RATE_200_MS;
					}else if(val > 50){
						to_write = APDS9999_LS_RATE_100_MS;
					}else if(val > 25){
						to_write = APDS9999_LS_RATE_50_MS;
					}else{
						to_write = APDS9999_LS_RATE_25_MS;
					}
					// TODO may add some debug info

					// this performs a read, modify, write cycle
					/* regmap, register to write, mask to use, what to write */
					ret = regmap_update_bits(data->regmap, APDS9999_REG_LS_MEAS_RATE, APDS9999_LS_RATE, to_write);
					if(ret){
						dev_err(indio_dev, "failed updating light sensor measurment rate.\n");
						return ret;
					}

					break;
				default:
					return -EINVAL;
			}
			break;
		case IIO_CHAN_INFO_HARDWAREGAIN:
			// Just the light channels have a gain that can be set in LS_GAIN
			if(chan->type == IIO_INTENSITY) {
					if(val > 9){
						to_write = APDS9999_LS_GAIN_RANGE_18;
					}else if(val > 6){
						to_write = APDS9999_LS_GAIN_RANGE_9;
					}else if(val > 3){
						to_write = APDS9999_LS_GAIN_RANGE_6;
					}else if(val > 1){
						to_write = APDS9999_LS_GAIN_RANGE_3;
					}else{
						to_write = APDS9999_LS_GAIN_RANGE_1;
					}
					// TODO may add some debug info

					// this performs a read, modify, write cycle
					/* regmap, register to write, mask to use, what to write */
					ret = regmap_update_bits(data->regmap, APDS9999_REG_LS_GAIN, APDS9999_LS_GAIN_RANGE, to_write);
					if(ret){
						dev_err(indio_dev, "failed updating light sensor gain.\n");
						return ret;
					}
			}
			break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info apds9999_info = {
	.read_raw = apds9999_read_raw,
	.write_raw = apds9999_write_raw,
	// TODO attributes
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

	// we init our regmap for our i2c client - devm still cause we want it managed
	data->regmap = devm_regmap_init_i2c(client, &apds9999_regmap_config);
	// errors get encoded in the upper address space, since they are small numbers. This macro checks for error
	if (IS_ERR(data->regmap)) {
		// log to kernel error log if init failed
		dev_err(&client->dev, "regmap init failed.\n");
		// this macro extracts the error
		return PTR_ERR(data->regmap);
	}

	data->client = client;
	data->indio_dev = indio_dev;

	// TODO

	// register the driver for this iio device, return if it fails
	ret = devm_iio_device_register(indio_dev);
	if (ret)
		return ret;

	dev_info("Hello world from apds9999");
	return 0;
}


// This function is called when the kernel unloads the driver
// We can release the i2c_client handle
static int apds9999_remove(struct i2c_client *client){

	// Not sure if we will need this. From a sensor perspective as well as from a kernel one.

	// TODO
	dev_info("Goodbye world from apds9999");
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
