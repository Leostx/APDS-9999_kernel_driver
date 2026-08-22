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
 * - [ ] power management basics: PS_EN / LS_EN / RGB_MODE now toggleable
 *       via sysfs (ps_enable, ls_enable, rgb_mode) - runtime PM / autosuspend
 *       and regulator handling still missing: dev_pm_ops
 * - [ ] implement triggers
 * - [ ] active_scan_mask
 * - [ ] IR should be valid also without RGB mode
 * - [ ] mutex where?
 * - [ ] LS DATA STATUS
 * - [ ] test events
 * - [ ] make read only attributes read only also in sysfs:
 * 	        The standard iio core way is to expose the same permissions for all attributes.
 *          In our case we return just an EINVAL error when writing to read-only attributes.
 *          This could be mitigated by moving the info_mask to an ext_info.
 *          For simplicity we keep it as is for now.
 * - [ ]
 * - [ ]
 * - [ ]
 * - [ ]
 *
 */

#include "linux/iio/types.h"
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
#include <linux/iio/sysfs.h>    // IIO_DEVICE_ATTR for the custom sysfs attributes
#include <linux/kstrtox.h>       // kstrtobool parses sysfs user input to boolean
#include <linux/sysfs.h>         // sysfs_emit formats sysfs reads for custom attributes
#include <linux/interrupt.h>     // request_threaded_irq, IRQ_WAKE_THREAD, IRQF_TRIGGER_LOW
#include <linux/iio/events.h>    // iio_event_spec, iio_push_event, IIO_EV_TYPE_THRESH, etc.

// Driver Name - done as define, since we use it multiple times
#define APDS9999_DRIVER_NAME 	"apds9999"

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

// defining the same as REGFIELDS
#define APDS9999_FIELD_CTRL_SAI_PS      REG_FIELD(APDS9999_REG_MAIN_CTRL, 6, 6)
#define APDS9999_FIELD_CTRL_SAI_LS      REG_FIELD(APDS9999_REG_MAIN_CTRL, 5, 5)
#define APDS9999_FIELD_CTRL_SW_RESET    REG_FIELD(APDS9999_REG_MAIN_CTRL, 4, 4)
#define APDS9999_FIELD_CTRL_RGB_MODE    REG_FIELD(APDS9999_REG_MAIN_CTRL, 2, 2)
#define APDS9999_FIELD_CTRL_LS_EN       REG_FIELD(APDS9999_REG_MAIN_CTRL, 1, 1)
#define APDS9999_FIELD_CTRL_PS_EN       REG_FIELD(APDS9999_REG_MAIN_CTRL, 0, 0)

// The following are the bits in PS_VCSEL - writing them restarts the PS state machine
#define APDS9999_PS_VCSEL_FREQ      GENMASK(6, 4)
#define APDS9999_PS_VCSEL_CURR      GENMASK(2, 0)

// defining the same as REGFIELDS
#define APDS9999_FIELD_PS_VCSEL_FREQ    REG_FIELD(APDS9999_REG_PS_VCSEL, 4, 6)
#define APDS9999_FIELD_PS_VCSEL_CURR    REG_FIELD(APDS9999_REG_PS_VCSEL, 0, 2)



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

// defining the same as REGFIELDS
#define APDS9999_FIELD_PS_RESO          REG_FIELD(APDS9999_REG_PS_MEAS_RATE, 3, 4)
#define APDS9999_FIELD_PS_RATE          REG_FIELD(APDS9999_REG_PS_MEAS_RATE, 0, 2)

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

// defining the same as REGFIELDS
#define APDS9999_FIELD_LS_RESO          REG_FIELD(APDS9999_REG_LS_MEAS_RATE, 4, 6)
#define APDS9999_FIELD_LS_RATE          REG_FIELD(APDS9999_REG_LS_MEAS_RATE, 0, 2)

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

// defining the same as REGFIELDS
#define APDS9999_FIELD_LS_GAIN_RANGE    REG_FIELD(APDS9999_REG_LS_GAIN, 0, 2)

/* Possible LS Gain values */
#define APDS9999_LS_GAIN_RANGE_1      0b000
#define APDS9999_LS_GAIN_RANGE_3      0b001	/* default */
#define APDS9999_LS_GAIN_RANGE_6      0b010
#define APDS9999_LS_GAIN_RANGE_9      0b011
#define APDS9999_LS_GAIN_RANGE_18     0b100

// The following are the bits in the PART_ID
#define APDS9999_ID_PART	   		GENMASK(7, 4)	/* Part number id */
#define APDS9999_ID_REVI	   		GENMASK(3, 0)	/* revision id */

// defining the same as REGFIELDS
#define APDS9999_FIELD_ID_PART          REG_FIELD(APDS9999_REG_PART_ID, 4, 7)
#define APDS9999_FIELD_ID_REVI          REG_FIELD(APDS9999_REG_PART_ID, 0, 3)

// The following are the bits in the MAIN_STATUS
#define APDS9999_STATUS_POS         BIT(5)	/* Power On Status*/
#define APDS9999_STATUS_LS_INT      BIT(4)	/* interrupt occured for ls*/
#define APDS9999_STATUS_LS_DATA 	BIT(3)	/* new ls data is ready*/
#define APDS9999_STATUS_PS_INT      BIT(1)	/* interrupt occured for ps*/
#define APDS9999_STATUS_PS_DATA 	BIT(0)	/* new ps data is ready*/

// defining the same as REGFIELDS
#define APDS9999_FIELD_STATUS_POS       REG_FIELD(APDS9999_REG_MAIN_STATUS, 5, 5)
#define APDS9999_FIELD_STATUS_LS_INT    REG_FIELD(APDS9999_REG_MAIN_STATUS, 4, 4)
#define APDS9999_FIELD_STATUS_LS_DATA   REG_FIELD(APDS9999_REG_MAIN_STATUS, 3, 3)
#define APDS9999_FIELD_STATUS_PS_INT    REG_FIELD(APDS9999_REG_MAIN_STATUS, 1, 1)
#define APDS9999_FIELD_STATUS_PS_DATA   REG_FIELD(APDS9999_REG_MAIN_STATUS, 0, 0)

// The following are the special bits for PS_DATA - regards PS_DATA_1
#define APDS9999_REG_PS_DATA_1_OVRFLW	BIT(3)	/* does the measurement lie outside of the measurable range */

// defining the same as REGFIELDS
#define APDS9999_FIELD_PS_DATA_1_OVRFLW REG_FIELD(APDS9999_REG_PS_DATA_1, 3, 3)

// The following are the bits for the INT_CFG
#define APDS9999_INT_CFG_LS_INT_SEL			 GENMASK(5, 4)
#define APDS9999_INT_CFG_LS_VAR_MODE         BIT(3)  /* LS interrupt mode: 0=threshold, 1=variation */
#define APDS9999_INT_CFG_LS_INT_EN           BIT(2)  /* LS interrupt enabled */
#define APDS9999_INT_CFG_PS_LOGIC_MODE       BIT(1)  /* 0=INT signal is active until status is cleared, 1=INT updated after every measurement */
#define APDS9999_INT_CFG_PS_INT_EN           BIT(0)  /* PS interrupt enabled */

// defining the same as REGFIELDS
#define APDS9999_FIELD_INT_CFG_LS_INT_SEL       REG_FIELD(APDS9999_REG_INT_CFG, 4, 5)
#define APDS9999_FIELD_INT_CFG_LS_VAR_MODE      REG_FIELD(APDS9999_REG_INT_CFG, 3, 3)
#define APDS9999_FIELD_INT_CFG_LS_INT_EN        REG_FIELD(APDS9999_REG_INT_CFG, 2, 2)
#define APDS9999_FIELD_INT_CFG_PS_LOGIC_MODE    REG_FIELD(APDS9999_REG_INT_CFG, 1, 1)
#define APDS9999_FIELD_INT_CFG_PS_INT_EN        REG_FIELD(APDS9999_REG_INT_CFG, 0, 0)

/* Possible INT_CFG LS_INT_SEL values */
#define APDS9999_INT_CFG_LS_INT_SEL_IR			0b00
#define APDS9999_INT_CFG_LS_INT_SEL_GREEN_ALS	0b01	/* default */
#define APDS9999_INT_CFG_LS_INT_SEL_RED			0b10
#define APDS9999_INT_CFG_LS_INT_SEL_BLUE		0b11


// The following are the bits for the INT_PST
/* sets the number of similar consecutive ints, before the int is asserted */
#define APDS9999_INT_PST_LS_PERS			 GENMASK(7, 4)
#define APDS9999_INT_PST_PS_PERS			 GENMASK(3, 0)

// defining the same as REGFIELDS
#define APDS9999_FIELD_INT_PST_LS_PERS  REG_FIELD(APDS9999_REG_INT_PST, 4, 7)
#define APDS9999_FIELD_INT_PST_PS_PERS  REG_FIELD(APDS9999_REG_INT_PST, 0, 3)

// The following are the bits for the PS_CAN
#define APDS9999_PS_CAN_DIG_HIGH    GENMASK(2, 0)  /* MSB of digital cancellation */
#define APDS9999_PS_CAN_ANA         GENMASK(7, 3)  /* analog cancellation */

#define APDS9999_FIELD_PS_CAN_DIG_HIGH  REG_FIELD(APDS9999_REG_PS_CAN_1, 0, 2)
#define APDS9999_FIELD_PS_CAN_ANA       REG_FIELD(APDS9999_REG_PS_CAN_1, 3, 7)


/* DEFINE DEFAULT VALUES - taken from the datasheet */
// FIELD_PREP_CONST is used to fill just a subset of bits.

#define APDS9999_REG_MAIN_CTRL_DEF              0x00 											/* 0x00 */
#define APDS9999_REG_PS_VCSEL_DEF																/* 0x36 */ \
			FIELD_PREP_CONST(APDS9999_PS_VCSEL_FREQ, APDS9999_PS_VCSEL_FREQ_60kHz) | \
			FIELD_PREP_CONST(APDS9999_PS_VCSEL_CURR, APDS9999_PS_VCSEL_CURR_DEF)
#define APDS9999_REG_PS_PULSES_DEF				0x08 											/* 0x08 */
#define APDS9999_REG_PS_MEAS_RATE_DEF 															/* 0x05 */ \
		FIELD_PREP_CONST(APDS9999_PS_RESO, APDS9999_PS_RESO_8_BIT) | \
		FIELD_PREP_CONST(APDS9999_PS_RATE, APDS9999_PS_RATE_100_MS)
#define APDS9999_REG_LS_MEAS_RATE_DEF															/* 0x22 */ \
		FIELD_PREP_CONST(APDS9999_LS_RESO, APDS9999_LS_RESO_18_BIT_100_MS) | \
		FIELD_PREP_CONST(APDS9999_LS_RATE, APDS9999_LS_RATE_100_MS)
#define APDS9999_REG_LS_GAIN_DEF																/* 0x01 */ \
			FIELD_PREP_CONST(APDS9999_LS_GAIN_RANGE, APDS9999_LS_GAIN_RANGE_3)
#define APDS9999_REG_PART_ID_DEF				0xC2 											/* 0xc2 */
#define APDS9999_REG_MAIN_STATUS_DEF															/* 0x20 */ \
			FIELD_PREP_CONST(APDS9999_STATUS_POS, 1)
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
			FIELD_PREP_CONST(APDS9999_INT_CFG_LS_INT_SEL, APDS9999_INT_CFG_LS_INT_SEL_GREEN_ALS)
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
// gain is shared: all intensity channels map to the same physical LS_GAIN register
#define APDS9999_INTENSITY_CHANNEL(_color, _si) { 				                            \
	.type                    = IIO_INTENSITY,						                        \
	.modified                = 1,									                        \
	.channel2                = IIO_MOD_LIGHT_##_color,				                        \
	.address                 = APDS9999_REG_LS_DATA_##_color##_0,	                        \
	.scan_index              = _si,									                        \
	.scan_type               = APDS9999_INTENSITY_SCAN_TYPE,		                        \
	.info_mask_separate      = BIT(IIO_CHAN_INFO_RAW) |	BIT(IIO_CHAN_INFO_PROCESSED),       \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_HARDWAREGAIN) | BIT(IIO_CHAN_INFO_SCALE), \
	/* The following is for having the *_available file in the sysfs dir that runs read_avail when read */  \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_HARDWAREGAIN),                  \
}

/* ------------------- END IIO CHANNEL DEFINES ------------------- */

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
	/*
	*	APDS9999_REG_MAIN_STATUS:
	*       LS INTERRUPT STATUS, LS DATA STATUS, PS INTERRUPT STATUS, PS DATA STATUS
	*       are cleared after reading, therefore they are precious
	*/
	regmap_reg_range(APDS9999_REG_MAIN_STATUS, APDS9999_REG_MAIN_STATUS),
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

    .volatile_table = &apds9999_volatile_table,		/* These registers change on hardware events */
    .precious_table = &apds9999_precious_table,		/* These registers change hardware on reads */

	.reg_defaults = apds9999_reg_defaults,		/* default values of the registers */
    .num_reg_defaults = ARRAY_SIZE(apds9999_reg_defaults),

	.max_register = APDS9999_REG_LS_THRES_VAR,
	.cache_type = REGCACHE_MAPLE,				/* this is the type of cache. Seems the best tradeoff */

	//TODO maybe wee want to add ranges for the consecutive reads
};


// Here we define the regmap fields
static const struct reg_field apds9999_reg_fields[] = {
	APDS9999_FIELD_CTRL_SAI_PS,
	APDS9999_FIELD_CTRL_SAI_LS,
	APDS9999_FIELD_CTRL_SW_RESET,
	APDS9999_FIELD_CTRL_RGB_MODE,
	APDS9999_FIELD_CTRL_LS_EN,
	APDS9999_FIELD_CTRL_PS_EN,
	APDS9999_FIELD_PS_VCSEL_FREQ,
	APDS9999_FIELD_PS_VCSEL_CURR,
	APDS9999_FIELD_PS_RESO,
	APDS9999_FIELD_PS_RATE,
	APDS9999_FIELD_LS_RESO,
	APDS9999_FIELD_LS_RATE,
	APDS9999_FIELD_LS_GAIN_RANGE,
	APDS9999_FIELD_ID_PART,
	APDS9999_FIELD_ID_REVI,
	APDS9999_FIELD_STATUS_POS,
	APDS9999_FIELD_STATUS_LS_INT,
	APDS9999_FIELD_STATUS_LS_DATA,
	APDS9999_FIELD_STATUS_PS_INT,
	APDS9999_FIELD_STATUS_PS_DATA,
	APDS9999_FIELD_PS_DATA_1_OVRFLW,
	APDS9999_FIELD_INT_CFG_LS_INT_SEL,
	APDS9999_FIELD_INT_CFG_LS_VAR_MODE,
	APDS9999_FIELD_INT_CFG_LS_INT_EN,
	APDS9999_FIELD_INT_CFG_PS_LOGIC_MODE,
	APDS9999_FIELD_INT_CFG_PS_INT_EN,
	APDS9999_FIELD_INT_PST_LS_PERS,
	APDS9999_FIELD_INT_PST_PS_PERS,
	APDS9999_FIELD_PS_CAN_DIG_HIGH,
	APDS9999_FIELD_PS_CAN_ANA,
};

// this are the indices into apds9999_data.regfield[]
// we should use these to access the regmap fields
enum apds9999_rf {
	APDS9999_RF_CTRL_SAI_PS = 0,
	APDS9999_RF_CTRL_SAI_LS,
	APDS9999_RF_CTRL_SW_RESET,
	APDS9999_RF_CTRL_RGB_MODE,
	APDS9999_RF_CTRL_LS_EN,
	APDS9999_RF_CTRL_PS_EN,
	APDS9999_RF_PS_VCSEL_FREQ,
	APDS9999_RF_PS_VCSEL_CURR,
	APDS9999_RF_PS_RESO,
	APDS9999_RF_PS_RATE,
	APDS9999_RF_LS_RESO,
	APDS9999_RF_LS_RATE,
	APDS9999_RF_LS_GAIN_RANGE,
	APDS9999_RF_ID_PART,
	APDS9999_RF_ID_REVI,
	APDS9999_RF_STATUS_POS,
	APDS9999_RF_STATUS_LS_INT,
	APDS9999_RF_STATUS_LS_DATA,
	APDS9999_RF_STATUS_PS_INT,
	APDS9999_RF_STATUS_PS_DATA,
	APDS9999_RF_PS_DATA_1_OVRFLW,
	APDS9999_RF_INT_CFG_LS_INT_SEL,
	APDS9999_RF_INT_CFG_LS_VAR_MODE,
	APDS9999_RF_INT_CFG_LS_INT_EN,
	APDS9999_RF_INT_CFG_PS_LOGIC_MODE,
	APDS9999_RF_INT_CFG_PS_INT_EN,
	APDS9999_RF_INT_PST_LS_PERS,
	APDS9999_RF_INT_PST_PS_PERS,
	APDS9999_RF_PS_CAN_DIG_HIGH,
	APDS9999_RF_PS_CAN_ANA,
	APDS9999_RF_COUNT,
};

/* ------------------- END REGMAP CONFIG ------------------- */

/* ------------------- LOOK UP TABLES ------------------- */
// This is what is used for conversion and what the *_available sysfs attributes show

// LUT for VCSEL frequency (kHz)
static const unsigned int apds9999_vcsel_freq_lut[] = { 60, 70, 80, 90, 100 }; /* kHz */

// LUT for VCSEL drive current (mA)
// APDS9999_PS_VCSEL_CURR_DEF (0b110) is the reset default but its mA value is not documented
static const unsigned int apds9999_vcsel_curr_lut[] = { 10, 25 }; /* mA */

// LUT for PS resolution in bits, indexed by register field value (0–3)
static const unsigned int apds9999_ps_reso_lut[] = { 8, 9, 10, 11 }; /* bits */

// LUT for PS measurement rate in microseconds, indexed by (field_val - 1) for values 1–7
// 0b000 is reserved; 0b001 = 6.25 ms, ..., 0b111 = 400 ms
static const unsigned int apds9999_ps_rate_lut[] = { 6250, 12500, 25000, 50000, 100000, 200000, 400000 }; /* µs */

// LUT for LS resolution in bits, indexed by register field value (0–5)
// 0b110 and 0b111 are reserved and not present in the table
static const unsigned int apds9999_ls_reso_lut[] = { 20, 19, 18, 17, 16, 13 }; /* bits */

// LUT for LS measurement rate in milliseconds, indexed by register field value (0–6)
static const unsigned int apds9999_ls_rate_lut[] = { 25, 50, 100, 200, 500, 1000, 2000, 2000 }; /* ms */

// LUT for LS gain multiplier, indexed by register field value (0–4)
// bits 0b101 through 0b111 are reserved and not present in the table
// type is int so that read_avail can hand the pointer directly to the IIO core
static const int apds9999_ls_gain_lut[] = { 1, 3, 6, 9, 18 }; /* x */

// Range table for digital cancellation level PS_CAN. {min, step, max}
static const int apds9999_ps_calibbias_range[] = { 0, 1, 2047 };

// Range table for analog cancellation level PS_CAN_ANA. {min, step, max}
static const int apds9999_ps_ana_can_range[] = { 0, 1, 31 };


// This table is for converting the light sensor readings to mLux values - this comes from the datasheet
// Resolution (mLux/count) indexed by [gain][resolution]
// Gain indices: 0=1x, 1=3x, 2=6x, 3=9x, 4=18x
// Resolution indices: 0=20bit, 1=19bit, 2=18bit, 3=17bit, 4=16bit
static const int ls_mlux_conversion_map_milli[5][5] = {
    { 136, 273, 548, 1099, 2193 }, 			/* 1x   */
    { 45,  90,  180, 359,  722 }, 			/* 3x   */
    { 22,  45,  90,  179,  360 }, 			/* 6x   */
    { 15,  30,  59,  119,  239 }, 			/* 9x   */
    { 7,   15,  29,   59,  117 }, 			/* 18x  */
};


// Maps the 2-bit LS_INT_SEL register value
static const char * const apds9999_ls_int_sel_names[] = {
	[APDS9999_INT_CFG_LS_INT_SEL_IR]        = "ir",
	[APDS9999_INT_CFG_LS_INT_SEL_GREEN_ALS] = "green",
	[APDS9999_INT_CFG_LS_INT_SEL_RED]       = "red",
	[APDS9999_INT_CFG_LS_INT_SEL_BLUE]      = "blue",
};

// Maps the 1-bit PS_LOGIC_MODE register value
// latched: INT signal stays active until the status register is cleared (bit=0)
// pulsed:  INT signal is updated after every PS measurement             (bit=1)
static const char * const apds9999_ps_logic_mode_names[] = {
	[0] = "latched",
	[1] = "pulsed",
};

/* ------------------- END LOOK UP TABLES ------------------- */

/* ------------------- AVAILABLE VALUE TABLES ------------------- */

enum apds9999_uint_avail_idx {
	APDS9999_UINT_AVAIL_VCSEL_FREQ,
	APDS9999_UINT_AVAIL_VCSEL_CURR,
	APDS9999_UINT_AVAIL_PS_RESO,
	APDS9999_UINT_AVAIL_PS_RATE,
	APDS9999_UINT_AVAIL_LS_RESO,
	APDS9999_UINT_AVAIL_LS_RATE,
};

static const struct { const unsigned int *lut; size_t count; } apds9999_uint_avail[] = {
	[APDS9999_UINT_AVAIL_VCSEL_FREQ] = { apds9999_vcsel_freq_lut, ARRAY_SIZE(apds9999_vcsel_freq_lut) },
	[APDS9999_UINT_AVAIL_VCSEL_CURR] = { apds9999_vcsel_curr_lut, ARRAY_SIZE(apds9999_vcsel_curr_lut) },
	[APDS9999_UINT_AVAIL_PS_RESO]    = { apds9999_ps_reso_lut,    ARRAY_SIZE(apds9999_ps_reso_lut) },
	[APDS9999_UINT_AVAIL_PS_RATE]    = { apds9999_ps_rate_lut,    ARRAY_SIZE(apds9999_ps_rate_lut) },
	[APDS9999_UINT_AVAIL_LS_RESO]    = { apds9999_ls_reso_lut,    ARRAY_SIZE(apds9999_ls_reso_lut) },
	[APDS9999_UINT_AVAIL_LS_RATE]    = { apds9999_ls_rate_lut,    ARRAY_SIZE(apds9999_ls_rate_lut) },
};

enum apds9999_str_avail_idx {
	APDS9999_STR_AVAIL_LS_INT_SEL,
	APDS9999_STR_AVAIL_PS_LOGIC_MODE,
};

static const struct { const char * const *names; size_t count; } apds9999_str_avail[] = {
	[APDS9999_STR_AVAIL_LS_INT_SEL]    = { apds9999_ls_int_sel_names,    ARRAY_SIZE(apds9999_ls_int_sel_names) },
	[APDS9999_STR_AVAIL_PS_LOGIC_MODE] = { apds9999_ps_logic_mode_names, ARRAY_SIZE(apds9999_ps_logic_mode_names) },
};

/* ------------------- END AVAILABLE VALUE TABLES ------------------- */

/* ------------------- IIO EVENTS ------------------- */
// The type of interrupt that our sensor produces mandates that we use events and not triggers
// The sensor generates an interrupt when the reading crosses a threshold

// This defines the events for the proximity sensor
static const struct iio_event_spec apds9999_ps_events[] = {
	{
	    /* This defines the event for the upper threshold crossing. It is done to set the value of PS_THRES_UP */
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
	    /* Same thing for lower threshold. Should set the value of PS_THRES_LOW */
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
	    /* This is for both events. Since we have single controll registers to enable and set the persistence. INT_CFG and INT_PST  */
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_NONE,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) | BIT(IIO_EV_INFO_PERIOD),
	},
};

// This defines the events for the light sensor. The logic is as the ps one
static const struct iio_event_spec apds9999_ls_events[] = {
	{
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type          = IIO_EV_TYPE_THRESH,
		.dir           = IIO_EV_DIR_NONE,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) | BIT(IIO_EV_INFO_PERIOD),
	},
	{
		.type          = IIO_EV_TYPE_CHANGE,
		.dir           = IIO_EV_DIR_NONE,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
};

/* ------------------- END IIO EVENTS ------------------- */

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
		.info_mask_separate           = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_CALIBBIAS),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_CALIBBIAS),
		.event_spec          = apds9999_ps_events,
		.num_event_specs     = ARRAY_SIZE(apds9999_ps_events),
	},

	APDS9999_INTENSITY_CHANNEL(RED, 1),
	APDS9999_INTENSITY_CHANNEL(GREEN, 2),
	APDS9999_INTENSITY_CHANNEL(BLUE, 3),
	APDS9999_INTENSITY_CHANNEL(IR, 4),

	/* Ambien Light Sensor (ALS) - This is the same as the green channel, it depends on the configuration */
	{
		.type                    = IIO_LIGHT,
		.address                 = APDS9999_REG_LS_DATA_GREEN_0,
		.scan_index              = 5,
		.scan_type               = APDS9999_INTENSITY_SCAN_TYPE,
		.info_mask_separate      = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_HARDWAREGAIN) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.event_spec          = apds9999_ls_events,
		.num_event_specs     = ARRAY_SIZE(apds9999_ls_events),
	},

};

// This is the type of struct that will eventually hold the data that our driver needs to function
struct apds9999_data {
	struct i2c_client *client;
	struct iio_dev *indio_dev;

	struct regmap *regmap;
	struct regmap_field *regfield[APDS9999_RF_COUNT];

	struct mutex lock;
};

// this function reads the raw value from the proximity sensor into val
static int apds9999_read_ps_raw(struct apds9999_data *data, unsigned int address, int *val){
	// variable to hold the resolution setting
	unsigned int setting;
	// regmap bulk read result
	__le16 regs;
	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	// read the resolution setting. This should get cached by regmap if read multiple times consecutively
	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_RESO], &setting);
	// if regmap reading the settings failed, return early with the error code
	if(ret){
		dev_err(&data->indio_dev->dev, "regmap reading ps resolution failed.\n");
		return ret;
	}

	// bulk read PS_DATA_0 and PS_DATA_1
	ret = regmap_bulk_read(data->regmap, address, &regs, 2);
	if (!ret) {
        // get the full 16 bit value from the regmap bulk read result and convert it to cpu endianness
		unsigned int raw = le16_to_cpu(regs);

		// check if the measurement has overflown, by masking out the overflow bit
		// we have to shift by 8 since we are using the full 16 bits
		// return -1 if yes, otherwise return the reading since all more significant bits are 0
		if (raw & (APDS9999_REG_PS_DATA_1_OVRFLW << 8))
			*val = -1;
		else
			*val = raw;
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
	// we have to initialize it to 0 to avoid garbage values
	__le32 buf = 0;
	// regmap bulk read takes the number of bytes to read as the last argument
	ret = regmap_bulk_read(data->regmap, address, &buf, 3);
	if (!ret){
		// convert the final value to cpu endianness and save it in val
		*val = le32_to_cpu(buf);

		// if ret is 0, everything went fine. Inform the caller that we read an int
		ret = IIO_VAL_INT;
	}

	return ret;
}

static int apds9999_read_ls_processed(struct apds9999_data *data, unsigned int address, int *val, int *val2){
    // variable to hold the resolution setting
	unsigned int reso;
	// variable to hold the gain setting
	unsigned int gain;

	int ret = -EINVAL;

	// read the resolution field
	ret = regmap_field_read(data->regfield[APDS9999_RF_LS_RESO], &reso);
	if(ret){
		dev_err(&data->indio_dev->dev, "regmap reading ls resolution failed.\n");
		return ret;
	}
	// read the gain field
	ret = regmap_field_read(data->regfield[APDS9999_RF_LS_GAIN_RANGE], &gain);
	if(ret){
		dev_err(&data->indio_dev->dev, "regmap reading ls gain failed.\n");
		return ret;
	}

	// 13-bit mode (0b101) has no entry in the conversion table
	if(reso == APDS9999_LS_RESO_13_BIT_3_125_MS){
		dev_err(&data->indio_dev->dev, "13-bit ls resolution has no scaling factor.\n");
		return -EINVAL;
	}

	// undefined gain values (0b101-0b111) have no entry in the conversion table
	if(gain > APDS9999_LS_GAIN_RANGE_18){
		dev_err(&data->indio_dev->dev, "undefined ls gain value.\n");
		return -EINVAL;
	}

	// here we read the raw ls count into val
	ret = apds9999_read_ls_raw(data, address, val);
	if(ret < 0)
		return ret;

	// scale the raw value to lux using the gain/resolution lookup table
	*val  = *val * ls_mlux_conversion_map_milli[gain][reso];
	*val2 = 1000;

	return IIO_VAL_FRACTIONAL;
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
				case IIO_LIGHT: {
					// ALS (green channel) is only valid when RGB mode is disabled
					unsigned int rgb_mode;

					ret = regmap_field_read(data->regfield[APDS9999_RF_CTRL_RGB_MODE], &rgb_mode);
					if (ret)
						return ret;

					if (rgb_mode) {
						dev_err(&data->indio_dev->dev, "in rgb mode: cannot read the ALS channel raw.\n");
						return -EBUSY;
					}

					ret = apds9999_read_ls_raw(data, APDS9999_REG_LS_DATA_GREEN_0, val);
					break;
				}
				case IIO_INTENSITY: {
					// RGB channels are only valid when RGB mode is enabled
					unsigned int rgb_mode;

					// read the rgb mode from the register
					ret = regmap_field_read(data->regfield[APDS9999_RF_CTRL_RGB_MODE], &rgb_mode);
					if (ret)
						return ret;

					if (!rgb_mode){
					    dev_err(&data->indio_dev->dev, "not in rgb mode: cannot read rgb channels.\n");
						return -EBUSY;
					}

					ret = apds9999_read_ls_raw(data, chan->address, val);
					break;
				}
				default:
					ret = -EINVAL;
			}
			break;
		case IIO_CHAN_INFO_CALIBBIAS: {
			__le16 buf16;

			if (chan->type != IIO_PROXIMITY)
				return -EINVAL;

			mutex_lock(&data->lock);
			ret = regmap_bulk_read(data->regmap, APDS9999_REG_PS_CAN_0, &buf16, 2);
			mutex_unlock(&data->lock);

			if (ret)
				return ret;

			// Mask out the 5 MSBs since they are the analog cancellation level. Keep the lower 11
			*val = le16_to_cpu(buf16) & (APDS9999_PS_CAN_DIG_HIGH | 0xFF);

			return IIO_VAL_INT;
		}
		case IIO_CHAN_INFO_PROCESSED:
			switch (chan->type) {
				// IIO_LIGHT is the illuminance (ALS) channel
				// It is the greeen channel when the RGB mode is disabled
				case IIO_LIGHT: {
					unsigned int rgb_mode;

					// read the rgb mode from the register
					ret = regmap_field_read(data->regfield[APDS9999_RF_CTRL_RGB_MODE], &rgb_mode);
					if (ret)
						return ret;

					if (rgb_mode){
					    dev_err(&data->indio_dev->dev, "in rgb mode: cannot read the intensity channel.\n");
						return -EBUSY;
					}

					return apds9999_read_ls_processed(data, APDS9999_REG_LS_DATA_GREEN_0, val, val2);
				}
				case IIO_INTENSITY: {
					// RGB channels require RGB mode
					unsigned int rgb_mode;
					ret = regmap_field_read(data->regfield[APDS9999_RF_CTRL_RGB_MODE], &rgb_mode);
					if (ret)
						return ret;
					if (!rgb_mode) {
						dev_err(&data->indio_dev->dev, "not in rgb mode: cannot read rgb channels.\n");
						return -EBUSY;
					}

					return apds9999_read_ls_processed(data, chan->address, val, val2);
				}
				default:
					return -EINVAL;
			}
			break;
			case IIO_CHAN_INFO_HARDWAREGAIN: {
				// hardware gain is only available for light sensor channels
				unsigned int bits;

				if (chan->type != IIO_INTENSITY && chan->type != IIO_LIGHT)
					return -EINVAL;

				ret = regmap_field_read(data->regfield[APDS9999_RF_LS_GAIN_RANGE], &bits);
				if (ret) {
					dev_err(&data->indio_dev->dev, "regmap_field_read LS_GAIN_RANGE failed.\n");
					return ret;
				}

				// bits 0b101 through 0b111 are not used
				if (bits > APDS9999_LS_GAIN_RANGE_18)
					return -EIO;

				// return the actual gain multiplier as an integer value
				*val = apds9999_ls_gain_lut[bits];
				return IIO_VAL_INT;
			}
			case IIO_CHAN_INFO_SCALE: {
				unsigned int reso, gain;

				if (chan->type != IIO_LIGHT && chan->type != IIO_INTENSITY)
					return -EINVAL;

				ret = regmap_field_read(data->regfield[APDS9999_RF_LS_RESO], &reso);
				if (ret) {
					dev_err(&data->indio_dev->dev, "regmap_field_read LS_RESO failed.\n");
					return ret;
				}

				ret = regmap_field_read(data->regfield[APDS9999_RF_LS_GAIN_RANGE], &gain);
				if (ret) {
					dev_err(&data->indio_dev->dev, "regmap_field_read LS_GAIN_RANGE failed.\n");
					return ret;
				}

				// 13-bit mode (0b101) has no defined scaling factor
				if (reso == APDS9999_LS_RESO_13_BIT_3_125_MS) {
					dev_err(&data->indio_dev->dev, "13-bit ls resolution has no scaling factor.\n");
					return -EINVAL;
				}

				// gain values above 18x (0b100) are not defined
				if (gain > APDS9999_LS_GAIN_RANGE_18) {
					dev_err(&data->indio_dev->dev, "not defined LS gain value.\n");
					return -EIO;
				}

				// scale = mLux_per_count / 1000 => Lux/count
				*val  = ls_mlux_conversion_map_milli[gain][reso];
				*val2 = 1000;
				return IIO_VAL_FRACTIONAL;
			}
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
		case IIO_CHAN_INFO_HARDWAREGAIN: {
			// hardware gain can only be set for light sensor channels
			if (chan->type != IIO_INTENSITY && chan->type != IIO_LIGHT)
				return -EINVAL;

			if (val == 1)
				to_write = APDS9999_LS_GAIN_RANGE_1;
			else if (val == 3)
				to_write = APDS9999_LS_GAIN_RANGE_3;
			else if (val == 6)
				to_write = APDS9999_LS_GAIN_RANGE_6;
			else if (val == 9)
				to_write = APDS9999_LS_GAIN_RANGE_9;
			else if (val == 18)
				to_write = APDS9999_LS_GAIN_RANGE_18;
			else
				return -EINVAL;

			ret = regmap_field_write(data->regfield[APDS9999_RF_LS_GAIN_RANGE], to_write);
			if (ret) {
				dev_err(&data->indio_dev->dev, "regmap_field_write LS_GAIN_RANGE failed.\n");
				return ret;
			}
			break;
		}
		case IIO_CHAN_INFO_CALIBBIAS: {
			// This is the digital can level we are going to set
			u8 buf[2];
			// This is the value of the MSB register
			unsigned int can1;

			if (chan->type != IIO_PROXIMITY)
				return -EINVAL;

			// 11-bit digital cancellation level
			if (val < 0 || val > 2047)
				return -EINVAL;

			mutex_lock(&data->lock);

			// This reads the MSB register wich containts the higher digi can levels as well as the ana can
			ret = regmap_read(data->regmap, APDS9999_REG_PS_CAN_1, &can1);
			if (ret) {
				mutex_unlock(&data->lock);
				return ret;
			}

			// write the LSB byte which is digi can level for sure
			buf[0] = val & 0xFF;
			// write the ana can bits we read earlier, and the higher digican bits
			// we mask out all bits that are not ps_can_ana in the MSByte and concatenate it with
			// the shift of the value we want to write, to consider only the MSByte and then we mask out everything that is not ps_can_dig_high
			buf[1] = (can1 & APDS9999_PS_CAN_ANA) | ((val >> 8) & APDS9999_PS_CAN_DIG_HIGH);

			// now we bulk write the two bytes
			ret = regmap_bulk_write(data->regmap, APDS9999_REG_PS_CAN_0, buf, 2);

			mutex_unlock(&data->lock);

			if (ret)
				return ret;

			break;
		}
		default:
			ret = -EINVAL;
	}

	return ret;
}

// this function is called by the IIO core when userspace reads an "_available" sysfs file
// it returns the list of discrete valid values for the given info mask
static int apds9999_read_avail(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, const int **vals, int *type, int *length, long mask){
	switch (mask) {
		case IIO_CHAN_INFO_HARDWAREGAIN:
			// expose the five discrete gain multipliers {1, 3, 6, 9, 18} that LS_GAIN supports
			*vals   = apds9999_ls_gain_lut;
			*type   = IIO_VAL_INT;
			*length = ARRAY_SIZE(apds9999_ls_gain_lut);
			return IIO_AVAIL_LIST;
		case IIO_CHAN_INFO_CALIBBIAS:
			// expose the 11-bit digital cancellation range [0, 1, 2047] for in_proximity_calibbias
			if (chan->type != IIO_PROXIMITY)
				return -EINVAL;
			*vals   = apds9999_ps_calibbias_range;
			*type   = IIO_VAL_INT;
			*length = ARRAY_SIZE(apds9999_ps_calibbias_range);
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
	}
}

/* ------------------- IIO EVENT CALLBACKS ------------------- */

// This reads the controll registers for the events (interrupts). Sysfs: *_thresh_en
static int apds9999_read_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan, enum iio_event_type type, enum iio_event_direction dir){
	struct apds9999_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	switch (chan->type) {
	    case IIO_PROXIMITY:
	    	ret = regmap_field_read(data->regfield[APDS9999_RF_INT_CFG_PS_INT_EN], &val);
	    	break;
		case IIO_LIGHT:
		{
			unsigned int reg_val;

			ret = regmap_read(data->regmap, APDS9999_REG_INT_CFG, &reg_val);
			if (ret)
				break;

			if (type == IIO_EV_TYPE_CHANGE)
				// true if both LS_VAR_MODE and LS_INT_EN are set
				// !! is for normalizing to 1/0
				val = !!(reg_val & (APDS9999_INT_CFG_LS_INT_EN | APDS9999_INT_CFG_LS_VAR_MODE));

			else
				// true if LS_INT_EN is set and LS_VAR_MODE is clear
				val = (reg_val & APDS9999_INT_CFG_LS_INT_EN) && !(reg_val & APDS9999_INT_CFG_LS_VAR_MODE);

			break;
		}

	    default:
	    	return -EINVAL;
	}

	return ret ? ret : (int)val;
}

// This writes the controll registers for the events (interrupts). Sysfs: *_thresh_en
static int apds9999_write_event_config(struct iio_dev *indio_dev, const struct iio_chan_spec *chan, enum iio_event_type type, enum iio_event_direction dir, bool state){
	struct apds9999_data *data = iio_priv(indio_dev);

	// This is to say if the LS is in variance or threshold mode (0: threshold, 1: variance)
	bool var_mode = (type == IIO_EV_TYPE_CHANGE);

	switch (chan->type) {
    	case IIO_PROXIMITY:
    		return regmap_field_write(data->regfield[APDS9999_RF_INT_CFG_PS_INT_EN], state);
    	case IIO_LIGHT:
            // here we set the variance mode and the enable bit in one go since they are both in the INT_CFG register
            return regmap_update_bits(data->regmap, APDS9999_REG_INT_CFG,
                     APDS9999_INT_CFG_LS_VAR_MODE | APDS9999_INT_CFG_LS_INT_EN,
                     FIELD_PREP(APDS9999_INT_CFG_LS_VAR_MODE, var_mode) |
                     FIELD_PREP(APDS9999_INT_CFG_LS_INT_EN, state));
    	default:
    		return -EINVAL;
	}
}


// This reads the threshold value or persistence count. *_THRESH_UP or *_THRESH_LOW or INT_PST
// Sysfs: *_thresh_{rising,falling}_value or *_thresh_period
static int apds9999_read_event_value(struct iio_dev *indio_dev, const struct iio_chan_spec *chan, enum iio_event_type type, enum iio_event_direction dir, enum iio_event_info info, int *val, int *val2){
	struct apds9999_data *data = iio_priv(indio_dev);

	int ret;

	// This is the persistence filter value (1..16):
	// Number of consecutive out-of-range measurements before the interrupt is asserted in hardware
	if (info == IIO_EV_INFO_PERIOD) {

	    // for saving the read value from the register
		unsigned int reg_val;

		switch (chan->type) {
    		case IIO_PROXIMITY:
                ret = regmap_field_read(data->regfield[APDS9999_RF_INT_PST_PS_PERS], &reg_val);
    			break;
    		case IIO_LIGHT:
                ret = regmap_field_read(data->regfield[APDS9999_RF_INT_PST_LS_PERS], &reg_val);
    			break;
    		default:
    			return -EINVAL;
		}

		if (ret)
			return ret;

		// Here we sum one, as the persistence value starts at 1
		*val = reg_val + 1;

		return IIO_VAL_INT;
	}

	// This handles the IIO_EV_INFO_VALUE, reading the threshold registers
	switch (chan->type) {
    	case IIO_PROXIMITY: {
            // little-endian 16-bit buffer to save our two-byte register reads
            __le16 buf16;
            // select the right register based on the event direction
    		unsigned int reg = (dir == IIO_EV_DIR_RISING) ? APDS9999_REG_PS_THRES_UP_0 : APDS9999_REG_PS_THRES_LOW_0;

            // Here we lock the mutex, such that the register read is atomic, since it is not provided by the hardware
    		mutex_lock(&data->lock);
    		ret = regmap_bulk_read(data->regmap, reg, &buf16, 2);
    		mutex_unlock(&data->lock);

    		if (ret)
    			return ret;

            // we do not mask the upper bits since the datasheet guarantees they are 0
            // we just convert to host-endian int
    		*val = le16_to_cpu(buf16);

    		return IIO_VAL_INT;
    	}
    	case IIO_LIGHT: {
            // change event read from the LS_THRES_VAR register
            if (type == IIO_EV_TYPE_CHANGE) {
                // reads a single byte. Just three bits in reality
                unsigned int reg_val;

                ret = regmap_read(data->regmap, APDS9999_REG_LS_THRES_VAR, &reg_val);
                if (ret)
                    return ret;

                // convert register value to count: 0 -> 8, 1 -> 16, 2 -> 32, ...
                *val = 8 << reg_val;

                return IIO_VAL_INT;
            }

            // little-endian 32-bit buffer to save our three-byte register reads
            __le32 buf32;
            // threshold events: select upper or lower threshold register based on direction
            unsigned int reg = (dir == IIO_EV_DIR_RISING) ? APDS9999_REG_LS_THRES_UP_0 : APDS9999_REG_LS_THRES_LOW_0;

            mutex_lock(&data->lock);
            ret = regmap_bulk_read(data->regmap, reg, &buf32, 3);
            mutex_unlock(&data->lock);

            if (ret)
                return ret;

            // convert the final value to cpu endianness and save it in val
            *val = le32_to_cpu(buf32);

            return IIO_VAL_INT;
    	}
    	default:
    		return -EINVAL;
	}
}

// This writes the threshold value or persistence count. *_THRESH_UP or *_THRESH_LOW or INT_PST
// Sysfs: *_thresh_{rising,falling}_value or *_thresh_either_period
static int apds9999_write_event_value(struct iio_dev *indio_dev, const struct iio_chan_spec *chan, enum iio_event_type type, enum iio_event_direction dir, enum iio_event_info info, int val, int val2){
	struct apds9999_data *data = iio_priv(indio_dev);

	int ret;

	// this regards the persistence filter
	if (info == IIO_EV_INFO_PERIOD) {

		if (val < 1 || val > 16)
			return -EINVAL;

		// decrement val to convert from 1-based to 0-based persistence filter value
		val--;

		switch (chan->type) {
    		case IIO_PROXIMITY:
                ret = regmap_field_write(data->regfield[APDS9999_RF_INT_PST_PS_PERS], val);
    			break;
    		case IIO_LIGHT:
                ret = regmap_field_write(data->regfield[APDS9999_RF_INT_PST_LS_PERS], val);
    			break;
    		default:
    			return -EINVAL;
		}

		return ret;
	}

	// This handles the IIO_EV_INFO_VALUE, reading the threshold registers
	switch (chan->type) {
    	case IIO_PROXIMITY: {
    		__le16 buf16;

    		unsigned int reg = (dir == IIO_EV_DIR_RISING) ? APDS9999_REG_PS_THRES_UP_0 : APDS9999_REG_PS_THRES_LOW_0;

    		// PS threshold is 11-bit
    		if (val < 0 || val > 0x7FF)
    			return -EINVAL;

    		buf16 = cpu_to_le16((u16)val);

    		mutex_lock(&data->lock);
    		ret = regmap_bulk_write(data->regmap, reg, &buf16, 2);
    		mutex_unlock(&data->lock);

    		return ret;
    	}
    	case IIO_LIGHT: {
            // change event read from the LS_THRES_VAR register
            if (type == IIO_EV_TYPE_CHANGE) {
                // val must be a power of two in [8, 1024] (register field is 3 bits: 0–7)
                if (val < 8 || val > 1024 || !is_power_of_2(val))
                    return -EINVAL;

                // convert count to register value: 8 -> 0, 16 -> 1, 32 -> 2, ...
                unsigned int reg_val = ilog2(val) - 3;

                ret = regmap_write(data->regmap, APDS9999_REG_LS_THRES_VAR, reg_val);
                return ret;
            }

            // threshold event write to the LS_THRES_UP/LOW registers
    		__le32 buf32;

    		unsigned int reg = (dir == IIO_EV_DIR_RISING) ? APDS9999_REG_LS_THRES_UP_0 : APDS9999_REG_LS_THRES_LOW_0;

    		// LS threshold is 20-bit
    		if (val < 0 || val > 0xFFFFF)
    			return -EINVAL;

            buf32 = cpu_to_le32((u32)val);

    		mutex_lock(&data->lock);
    		ret = regmap_bulk_write(data->regmap, reg, &buf32, 3);
    		mutex_unlock(&data->lock);

    		return ret;
    	}
    	default:
    		return -EINVAL;
	}
}

/* ------------------- END IIO EVENT CALLBACKS ------------------- */

/* ------------------- INTERRUPT HANDLERS ------------------- */

// Hard-IRQ handler. Runs in interrupt context, cannot handle I2C
static irqreturn_t apds9999_irq_handler(int irq, void *p){
    // Wake the threaded handler
	return IRQ_WAKE_THREAD;
}


// This is the threaded IRQ handler it checks for interrupts and pushes events to userspace
static irqreturn_t apds9999_irq_thread(int irq, void *p){
	struct iio_dev *indio_dev = p;
	struct apds9999_data *data = iio_priv(indio_dev);

	// the status register is read into this
	unsigned int status;
	s64 timestamp;
	int ret;


	// reads and clears the main status register
	ret = regmap_read(data->regmap, APDS9999_REG_MAIN_STATUS, &status);
	if (ret) {
		dev_err(&data->client->dev, "failed to read MAIN_STATUS: %d\n", ret);
		return IRQ_HANDLED;
	}

	// get timestamp of event
	timestamp = iio_get_time_ns(indio_dev);

	// check if PS interrupt status is set
	if (status & APDS9999_STATUS_PS_INT) {
	    // we push the event with the dir either, since checking would require an additional read
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0, IIO_EV_TYPE_THRESH, IIO_EV_DIR_EITHER),
			       timestamp);
	}

	// check if LS interrupt status is set
	if (status & APDS9999_STATUS_LS_INT) {
        // we push the event with the dir either and the type threshold, since checking would require an additional read
        // TODO: do we want to do the additional reads?
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0, IIO_EV_TYPE_THRESH, IIO_EV_DIR_EITHER),
			       timestamp);
	}

	return IRQ_HANDLED;
}

/* ------------------- END INTERRUPT HANDLERS ------------------- */

/* -------------------------- CUSTOM SYSFS ATTRIBUTES -------------------------- */
// Here we have custom sysfs attributs to get the full control over our driver

// this function is for reading a boolean value from a register on the device through sysfs
static ssize_t apds9999_attr_bool_show(struct device *dev, struct device_attribute *attr, char *buf){
	// retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);
	// This holds the apds9999_rf enum index in its address field
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(attr);

	unsigned int val;
	int ret;

	// regmap_field_read knows the register, mask and shift
	ret = regmap_field_read(data->regfield[iio_attr->address], &val);
	if(ret){
		dev_err(&indio_dev->dev, "regmap_field_read failed for field %llu.\n",
			(unsigned long long)iio_attr->address);
		return ret;
	}

	return sysfs_emit(buf, "%u\n", val);
}

// this function is for writing a boolean value to a register on the device through sysfs
static ssize_t apds9999_attr_bool_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
	// retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);
	// This holds the apds9999_rf enum index in its address field
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(attr);

	// variable to hold the boolean value the user wrote
	bool enable;
	int ret;

	// kstrtobool parses the input to a boolean
	ret = kstrtobool(buf, &enable);
	if(ret)
		return ret;

	// regmap_field_write handles the read-modify-write, mask and shift internally;
	ret = regmap_field_write(data->regfield[iio_attr->address], enable ? 1 : 0);
	if(ret){
		dev_err(&indio_dev->dev, "regmap_field_write failed for field %llu.\n",
			(unsigned long long)iio_attr->address);
		return ret;
	}

	// returns the number of bytes consumed on success
	return len;
}


/* -------------------------- PS_VCSEL ATTRIBUTES -------------------------- */
// VCSEL modulation frequency (kHz) and drive current (mA) */

/* --- VCSEL frequency --- */

static ssize_t apds9999_vcsel_freq_show(struct device *dev, struct device_attribute *attr, char *buf){
    // retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_VCSEL_FREQ], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read PS_VCSEL_FREQ failed.\n");
		return ret;
	}

	// check that the bits read is within the valid range of the LUT
	if (bits >= APDS9999_PS_VCSEL_FREQ_60kHz && bits <= APDS9999_PS_VCSEL_FREQ_100kHz)
		return sysfs_emit(buf, "%ukHz\n", apds9999_vcsel_freq_lut[bits - APDS9999_PS_VCSEL_FREQ_60kHz]);

	// bit pattern not in table
	return sysfs_emit(buf, "raw:%u\n", bits);
}

static ssize_t apds9999_vcsel_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
    // retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	// read the input as an unsigned integer
	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// Take the plain bit pattern as input if available, otherwise round down to the nearest valid VCSEL frequency
	if(input >= APDS9999_PS_VCSEL_FREQ_60kHz && input <= APDS9999_PS_VCSEL_FREQ_100kHz)
		write_val = input;
	else if (input <= 60)
		write_val = APDS9999_PS_VCSEL_FREQ_60kHz;
	else if (input <= 70)
		write_val = APDS9999_PS_VCSEL_FREQ_70kHz;
	else if (input <= 80)
		write_val = APDS9999_PS_VCSEL_FREQ_80kHz;
	else if (input <= 90)
		write_val = APDS9999_PS_VCSEL_FREQ_90kHz;
	else
		write_val = APDS9999_PS_VCSEL_FREQ_100kHz;

	dev_info(&indio_dev->dev, "Proximity Sensor VCSEL pulse modulation frequency will be set to %ukHz (bits: %u).\n",
			apds9999_vcsel_freq_lut[write_val - APDS9999_PS_VCSEL_FREQ_60kHz], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_PS_VCSEL_FREQ], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write PS_VCSEL_FREQ failed.\n");
		return ret;
	}

	return len;
}

/* --- VCSEL current --- */

static ssize_t apds9999_vcsel_curr_show(struct device *dev, struct device_attribute *attr, char *buf) {
    // retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_VCSEL_CURR], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read PS_VCSEL_CURR failed.\n");
		return ret;
	}

	if (bits >= APDS9999_PS_VCSEL_CURR_10mA && bits <= APDS9999_PS_VCSEL_CURR_25mA)
		return sysfs_emit(buf, "%umA\n", apds9999_vcsel_curr_lut[bits - APDS9999_PS_VCSEL_CURR_10mA]);

	return sysfs_emit(buf, "raw:%u\n", bits);
}

static ssize_t apds9999_vcsel_curr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
    // retrieve the iio_dev, then the driver data associated with it
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	// read the input as an unsigned integer
	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// Take the plain bit pattern as input if available, otherwise round down to the nearest valid VCSEL current
	if (input >= APDS9999_PS_VCSEL_CURR_10mA && input <= APDS9999_PS_VCSEL_CURR_25mA)
		write_val = input;
	else if (input <= 10)
		write_val = APDS9999_PS_VCSEL_CURR_10mA;
	else
		write_val = APDS9999_PS_VCSEL_CURR_25mA;

	dev_info(&indio_dev->dev, "Proximity Sensor VCSEL current will be set to %umA (bits: %u).\n",
			apds9999_vcsel_curr_lut[write_val - APDS9999_PS_VCSEL_CURR_10mA], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_PS_VCSEL_CURR], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write PS_VCSEL_CURR failed.\n");
		return ret;
	}

	return len;
}

static ssize_t apds9999_uint_avail_show(struct device *dev, struct device_attribute *attr, char *buf){
    // index into apds9999_uint_avail is saved in the device attribute's address field
	unsigned int idx = to_iio_dev_attr(attr)->address;

	// pointer to the acctual lut
	const unsigned int *lut = apds9999_uint_avail[idx].lut;
	// size
	size_t count = apds9999_uint_avail[idx].count;

	int len = 0;
	size_t i;
	for (i = 0; i < count; i++)
		len += sysfs_emit_at(buf, len, i < count - 1 ? "%u " : "%u\n", lut[i]);

	return len;
}

static ssize_t apds9999_str_avail_show(struct device *dev, struct device_attribute *attr, char *buf){
    // index into apds9999_str_avail is saved in the device attribute's address field
	unsigned int idx = to_iio_dev_attr(attr)->address;

	// pointer to the actual lut containing the names
	const char * const *names = apds9999_str_avail[idx].names;
	// size
	size_t count = apds9999_str_avail[idx].count;

	int len = 0;
	size_t i;

	for (i = 0; i < count; i++)
		len += sysfs_emit_at(buf, len, i < count - 1 ? "%s " : "%s\n", names[i]);

	return len;
}

/* -------------------------- END PS_VCSEL ATTRIBUTES -------------------------- */

/* -------------------------- PS_PULSES ATTRIBUTE -------------------------- */

// Number of pulses emitted per PS measurement (0–255, raw 8-bit value)

static ssize_t apds9999_ps_pulses_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, APDS9999_REG_PS_PULSES, &val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_read PS_PULSES failed.\n");
		return ret;
	}

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t apds9999_ps_pulses_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 0xFF)
		return -EINVAL;

	dev_info(&indio_dev->dev, "Proximity Sensor pulse count will be set to %u.\n", val);

	ret = regmap_write(data->regmap, APDS9999_REG_PS_PULSES, val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_write PS_PULSES failed.\n");
		return ret;
	}

	return len;
}

/* -------------------------- END PS_PULSES ATTRIBUTE -------------------------- */

/* -------------------------- PS_MEAS_RATE ATTRIBUTES -------------------------- */
// PS resolution (bits) and measurement rate (µs)

/* --- PS resolution --- */

static ssize_t apds9999_ps_reso_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_RESO], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read PS_RESO failed.\n");
		return ret;
	}

	// bits is 2 bits wide (0–3), always in range
	return sysfs_emit(buf, "%u bit\n", apds9999_ps_reso_lut[bits]);
}

static ssize_t apds9999_ps_reso_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// accept raw register bits (0–3) or bit-count (8–11). Rounds to the nearest valid value
	if (input <= APDS9999_PS_RESO_11_BIT)
		write_val = input;
	else if (input <= 8)
		write_val = APDS9999_PS_RESO_8_BIT;
	else if (input <= 9)
		write_val = APDS9999_PS_RESO_9_BIT;
	else if (input <= 10)
		write_val = APDS9999_PS_RESO_10_BIT;
	else
		write_val = APDS9999_PS_RESO_11_BIT;

	dev_info(&indio_dev->dev, "Proximity Sensor resolution will be set to %u bit (bits: %u).\n",
			apds9999_ps_reso_lut[write_val], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_PS_RESO], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write PS_RESO failed.\n");
		return ret;
	}

	return len;
}

/* --- PS measurement rate --- */

static ssize_t apds9999_ps_meas_rate_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_RATE], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read PS_RATE failed.\n");
		return ret;
	}

	if (bits >= APDS9999_PS_RATE_6_25_MS && bits <= APDS9999_PS_RATE_400_MS)
		return sysfs_emit(buf, "%u us\n", apds9999_ps_rate_lut[bits - APDS9999_PS_RATE_6_25_MS]);

	// bit pattern 0b000 is reserved so maybe it will appear
	return sysfs_emit(buf, "raw:%u\n", bits);
}

static ssize_t apds9999_ps_meas_rate_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// accept raw register bits (1–7) or a rate in µs
	if (input >= APDS9999_PS_RATE_6_25_MS && input <= APDS9999_PS_RATE_400_MS)
		write_val = input;
	else if (input <= 6250)
		write_val = APDS9999_PS_RATE_6_25_MS;
	else if (input <= 12500)
		write_val = APDS9999_PS_RATE_12_5_MS;
	else if (input <= 25000)
		write_val = APDS9999_PS_RATE_25_MS;
	else if (input <= 50000)
		write_val = APDS9999_PS_RATE_50_MS;
	else if (input <= 100000)
		write_val = APDS9999_PS_RATE_100_MS;
	else if (input <= 200000)
		write_val = APDS9999_PS_RATE_200_MS;
	else
		write_val = APDS9999_PS_RATE_400_MS;

	dev_info(&indio_dev->dev, "Proximity Sensor measurement rate will be set to %u us (bits: %u).\n",
			apds9999_ps_rate_lut[write_val - APDS9999_PS_RATE_6_25_MS], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_PS_RATE], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write PS_RATE failed.\n");
		return ret;
	}

	return len;
}

/* -------------------------- END PS_MEAS_RATE ATTRIBUTES -------------------------- */

/* -------------------------- LS_MEAS_RATE ATTRIBUTES -------------------------- */
// LS resolution (bits) and measurement rate (ms) from the LS_MEAS_RATE register

/* --- LS resolution --- */

static ssize_t apds9999_ls_reso_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_LS_RESO], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read LS_RESO failed.\n");
		return ret;
	}

	// bits 0b110 and 0b111 are reserved
	if (bits <= APDS9999_LS_RESO_13_BIT_3_125_MS)
		return sysfs_emit(buf, "%u bit\n", apds9999_ls_reso_lut[bits]);

	return sysfs_emit(buf, "raw:%u\n", bits);
}

static ssize_t apds9999_ls_reso_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// accept raw register bits (0–5) or a bit-count (13–20)
	if (input <= APDS9999_LS_RESO_13_BIT_3_125_MS)
		write_val = input;
	else if (input <= 13)
		write_val = APDS9999_LS_RESO_13_BIT_3_125_MS;
	else if (input <= 16)
		write_val = APDS9999_LS_RESO_16_BIT_25_MS;
	else if (input <= 17)
		write_val = APDS9999_LS_RESO_17_BIT_50_MS;
	else if (input <= 18)
		write_val = APDS9999_LS_RESO_18_BIT_100_MS;
	else if (input <= 19)
		write_val = APDS9999_LS_RESO_19_BIT_200_MS;
	else
		write_val = APDS9999_LS_RESO_20_BIT_400_MS;

	dev_info(&indio_dev->dev, "Light Sensor resolution will be set to %u bit (bits: %u).\n",
			apds9999_ls_reso_lut[write_val], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_LS_RESO], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write LS_RESO failed.\n");
		return ret;
	}

	return len;
}

/* --- LS measurement rate --- */

static ssize_t apds9999_ls_meas_rate_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int bits;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_LS_RATE], &bits);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_read LS_RATE failed.\n");
		return ret;
	}

	// bit pattern 0b111 is reserved
	if (bits <= APDS9999_LS_RATE_2000_MS)
		return sysfs_emit(buf, "%u ms\n", apds9999_ls_rate_lut[bits]);

	return sysfs_emit(buf, "raw:%u\n", bits);
}

static ssize_t apds9999_ls_meas_rate_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) {
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int input;
	unsigned int write_val;
	int ret;

	ret = kstrtouint(buf, 0, &input);
	if (ret)
		return ret;

	// accept raw register bits (0–6) or a rate in ms
	if (input <= APDS9999_LS_RATE_2000_MS)
		write_val = input;
	else if (input <= 25)
		write_val = APDS9999_LS_RATE_25_MS;
	else if (input <= 50)
		write_val = APDS9999_LS_RATE_50_MS;
	else if (input <= 100)
		write_val = APDS9999_LS_RATE_100_MS;
	else if (input <= 200)
		write_val = APDS9999_LS_RATE_200_MS;
	else if (input <= 500)
		write_val = APDS9999_LS_RATE_500_MS;
	else if (input <= 1000)
		write_val = APDS9999_LS_RATE_1000_MS;
	else
		write_val = APDS9999_LS_RATE_2000_MS;

	dev_info(&indio_dev->dev, "Light Sensor measurement rate will be set to %u ms (bits: %u).\n",
			apds9999_ls_rate_lut[write_val], write_val);

	ret = regmap_field_write(data->regfield[APDS9999_RF_LS_RATE], write_val);
	if (ret) {
		dev_err(&indio_dev->dev, "regmap_field_write LS_RATE failed.\n");
		return ret;
	}

	return len;
}

/* -------------------------- END LS_MEAS_RATE ATTRIBUTES -------------------------- */

/* -------------------------- LS_INT_SEL EVENT ATTRIBUTE -------------------------- */

static ssize_t apds9999_ls_int_sel_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_INT_CFG_LS_INT_SEL], &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", apds9999_ls_int_sel_names[val]);
}

static ssize_t apds9999_ls_int_sel_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	int ret;

	// sysfs_match_string handles trailing newlines and is case-insensitive
	ret = sysfs_match_string(apds9999_ls_int_sel_names, buf);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(data->regfield[APDS9999_RF_INT_CFG_LS_INT_SEL], ret);
	return ret ? ret : len;
}



/* -------------------------- END LS_INT_SEL EVENT ATTRIBUTE -------------------------- */

/* -------------------------- PS_LOGIC_MODE EVENT ATTRIBUTE -------------------------- */

static ssize_t apds9999_ps_logic_mode_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_INT_CFG_PS_LOGIC_MODE], &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", apds9999_ps_logic_mode_names[val]);
}

static ssize_t apds9999_ps_logic_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	int ret;

	// sysfs_match_string handles trailing newlines and is case-insensitive
	ret = sysfs_match_string(apds9999_ps_logic_mode_names, buf);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(data->regfield[APDS9999_RF_INT_CFG_PS_LOGIC_MODE], ret);
	return ret ? ret : len;
}

/* -------------------------- END PS_LOGIC_MODE EVENT ATTRIBUTE -------------------------- */

static IIO_DEVICE_ATTR(ls_int_sel, 0644, apds9999_ls_int_sel_show, apds9999_ls_int_sel_store, 0);
static IIO_DEVICE_ATTR(ls_int_sel_available, 0444, apds9999_str_avail_show, NULL, APDS9999_STR_AVAIL_LS_INT_SEL);
static IIO_DEVICE_ATTR(ps_logic_mode, 0644, apds9999_ps_logic_mode_show, apds9999_ps_logic_mode_store, 0);
static IIO_DEVICE_ATTR(ps_logic_mode_available, 0444, apds9999_str_avail_show, NULL, APDS9999_STR_AVAIL_PS_LOGIC_MODE);

static struct attribute *apds9999_event_attributes[] = {
	&iio_dev_attr_ls_int_sel.dev_attr.attr,
	&iio_dev_attr_ls_int_sel_available.dev_attr.attr,
	&iio_dev_attr_ps_logic_mode.dev_attr.attr,
	&iio_dev_attr_ps_logic_mode_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group apds9999_event_attribute_group = {
	.attrs = apds9999_event_attributes,
};


/* -------------------------- PS_CAN_ANA ATTRIBUTES -------------------------- */

static ssize_t apds9999_ps_ana_can_show(struct device *dev, struct device_attribute *attr, char *buf){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = regmap_field_read(data->regfield[APDS9999_RF_PS_CAN_ANA], &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t apds9999_ps_ana_can_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len){
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct apds9999_data *data = iio_priv(indio_dev);

	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	// 5-bit: 0-31
	if (val < 0 || val > 31)
		return -EINVAL;

	ret = regmap_field_write(data->regfield[APDS9999_RF_PS_CAN_ANA], val);
	if (ret)
		return ret;

	return len;
}

static ssize_t apds9999_ps_ana_can_available_show(struct device *dev, struct device_attribute *attr, char *buf){
	return sysfs_emit(buf, "[%d %d %d]\n", apds9999_ps_ana_can_range[0], apds9999_ps_ana_can_range[1], apds9999_ps_ana_can_range[2]);
}

/* -------------------------- END PS_CAN_ANA ATTRIBUTES -------------------------- */

// these macros generate the "iio_dev_attr_<name>" structs such that we have custom attributs in our sysfs directory

// these are the controll bits from the MAIN_CTRL register
static IIO_DEVICE_ATTR(ps_enable, 0644, apds9999_attr_bool_show, apds9999_attr_bool_store, APDS9999_RF_CTRL_PS_EN);
static IIO_DEVICE_ATTR(ls_enable, 0644, apds9999_attr_bool_show, apds9999_attr_bool_store, APDS9999_RF_CTRL_LS_EN);
static IIO_DEVICE_ATTR(rgb_mode, 0644, apds9999_attr_bool_show, apds9999_attr_bool_store, APDS9999_RF_CTRL_RGB_MODE);
static IIO_DEVICE_ATTR(sai_ps, 0644, apds9999_attr_bool_show, apds9999_attr_bool_store, APDS9999_RF_CTRL_SAI_PS);
static IIO_DEVICE_ATTR(sai_ls, 0644, apds9999_attr_bool_show, apds9999_attr_bool_store, APDS9999_RF_CTRL_SAI_LS);

// these are the controll bits from the PS_VCSEL register
static IIO_DEVICE_ATTR(ps_vcsel_freq_khz, 0644, apds9999_vcsel_freq_show, apds9999_vcsel_freq_store, 0);
static IIO_DEVICE_ATTR(ps_vcsel_freq_khz_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_VCSEL_FREQ);
static IIO_DEVICE_ATTR(ps_vcsel_curr_ma, 0644, apds9999_vcsel_curr_show, apds9999_vcsel_curr_store, 0);
static IIO_DEVICE_ATTR(ps_vcsel_curr_ma_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_VCSEL_CURR);

// PS_PULSES register: number of pulses per PS measurement
static IIO_DEVICE_ATTR(ps_pulses, 0644, apds9999_ps_pulses_show, apds9999_ps_pulses_store, 0);

// PS_CAN register: analog cancellation level
static IIO_DEVICE_ATTR(ps_analog_cancellation, 0644, apds9999_ps_ana_can_show, apds9999_ps_ana_can_store, 0);
static IIO_DEVICE_ATTR(ps_analog_cancellation_available, 0444, apds9999_ps_ana_can_available_show, NULL, 0);

// PS_MEAS_RATE register: PS resolution and measurement rate
static IIO_DEVICE_ATTR(ps_reso_bit, 0644, apds9999_ps_reso_show, apds9999_ps_reso_store, 0);
static IIO_DEVICE_ATTR(ps_reso_bit_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_PS_RESO);
static IIO_DEVICE_ATTR(ps_meas_rate_us, 0644, apds9999_ps_meas_rate_show, apds9999_ps_meas_rate_store, 0);
static IIO_DEVICE_ATTR(ps_meas_rate_us_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_PS_RATE);

// LS_MEAS_RATE register: LS resolution and measurement rate
static IIO_DEVICE_ATTR(ls_reso_bit, 0644, apds9999_ls_reso_show, apds9999_ls_reso_store, 0);
static IIO_DEVICE_ATTR(ls_reso_bit_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_LS_RESO);
static IIO_DEVICE_ATTR(ls_meas_rate_ms, 0644, apds9999_ls_meas_rate_show, apds9999_ls_meas_rate_store, 0);
static IIO_DEVICE_ATTR(ls_meas_rate_ms_available, 0444, apds9999_uint_avail_show, NULL, APDS9999_UINT_AVAIL_LS_RATE);

// list of custom attributes exposed to sysfs
static struct attribute *apds9999_attributes[] = {
    // MAIN_CTRL register
	&iio_dev_attr_ps_enable.dev_attr.attr,
	&iio_dev_attr_ls_enable.dev_attr.attr,
	&iio_dev_attr_rgb_mode.dev_attr.attr,
	&iio_dev_attr_sai_ps.dev_attr.attr,
	&iio_dev_attr_sai_ls.dev_attr.attr,
	// PS_VCSEL register
	&iio_dev_attr_ps_vcsel_freq_khz.dev_attr.attr,
	&iio_dev_attr_ps_vcsel_freq_khz_available.dev_attr.attr,
	&iio_dev_attr_ps_vcsel_curr_ma.dev_attr.attr,
	&iio_dev_attr_ps_vcsel_curr_ma_available.dev_attr.attr,
	// PS_PULSES register
	&iio_dev_attr_ps_pulses.dev_attr.attr,
	// PS_CAN register: analog cancellation
	&iio_dev_attr_ps_analog_cancellation.dev_attr.attr,
	&iio_dev_attr_ps_analog_cancellation_available.dev_attr.attr,
	// PS_MEAS_RATE register
	&iio_dev_attr_ps_reso_bit.dev_attr.attr,
	&iio_dev_attr_ps_reso_bit_available.dev_attr.attr,
	&iio_dev_attr_ps_meas_rate_us.dev_attr.attr,
	&iio_dev_attr_ps_meas_rate_us_available.dev_attr.attr,
	// LS_MEAS_RATE register
	&iio_dev_attr_ls_reso_bit.dev_attr.attr,
	&iio_dev_attr_ls_reso_bit_available.dev_attr.attr,
	&iio_dev_attr_ls_meas_rate_ms.dev_attr.attr,
	&iio_dev_attr_ls_meas_rate_ms_available.dev_attr.attr,

	NULL,	/* the attribute array must be NULL terminated */
};

static const struct attribute_group apds9999_attribute_group = {
	.attrs = apds9999_attributes,
};

static const struct iio_info apds9999_info = {
	.attrs              = &apds9999_attribute_group,	    /* exposes custom attributes in sysfs */
	.event_attrs        = &apds9999_event_attribute_group,	/* exposes custom attributes in events/ sysfs dir */

	.read_raw           = apds9999_read_raw,
	.write_raw          = apds9999_write_raw,
	.read_avail         = apds9999_read_avail,			/* exposes _available sysfs files for enumerable settings */

	/* The following is for the event interface (interrupts) */
	.read_event_config  = apds9999_read_event_config,
	.write_event_config = apds9999_write_event_config,
	.read_event_value   = apds9999_read_event_value,
	.write_event_value  = apds9999_write_event_value,
};

// this function gets called during probe to initialize the chip
// It checks the part id register to verify the chip is an APDS-9999
// and then resets it to a known state
// Finally, it enables both PS and LS in RGB_MODE by default
static int apds9999_chip_init(struct apds9999_data *data){
	// variable to hold the part id we read back from the sensor
	unsigned int part_id;
	// error code EINVAL is when the argument is invalid or out of range - negative since used as return value
	int ret = -EINVAL;

	// regmap_read takes the regmap, the register and a pointer to store the value there
	ret = regmap_read(data->regmap, APDS9999_REG_PART_ID, &part_id);
	if(ret){
		dev_err(&data->indio_dev->dev, "regmap reading PART_ID failed.\n");
		return ret;
	}

	// we check if we are actually talking to the right device
	if(part_id != APDS9999_REG_PART_ID_DEF){
		dev_err(&data->indio_dev->dev,
			"unexpected PART_ID 0x%02x (expected 0x%02x) - aborting probe.\n",
			part_id, APDS9999_REG_PART_ID_DEF);
		return -ENODEV; /* ENODEV: "No such device" */
	}


	// We write the SW_RESET bit to the MAIN_CTRL register to trigger the reset
	// So we reset the chip also during a warm reboot without a power cycle
	// This disables everything else since we write the full register, but it doesnt matter since we are performing the reset anyway
	// the error code we exclude is because the chip does not respond with an ack, since it has been reset
	ret = regmap_write(data->regmap, APDS9999_REG_MAIN_CTRL, APDS9999_CTRL_SW_RESET);
	// After a successful reset the chip does not ACK. So -EREMOTEIO is expected, everything else is a failure
	if (ret && ret != -EREMOTEIO) { /* EREMOTEIO: "Remote I/O error" */
		dev_err(&data->indio_dev->dev, "software reset failed: %d\n", ret);
		return ret;
	}

	// Sleep between 1-1.5ms to allow the reset to complete. The datasheet talks abaout 500us, so we are conservative
	usleep_range(1000, 1500);

	// by default we enable both PS and LS, and select RGB_MODE
	ret = regmap_write(data->regmap, APDS9999_REG_MAIN_CTRL,
			APDS9999_CTRL_PS_EN | APDS9999_CTRL_LS_EN | APDS9999_CTRL_RGB_MODE);
	if(ret){
		dev_err(&data->indio_dev->dev, "failed enabling PS/LS.\n");
		return ret;
	}

	return 0;
}

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

	mutex_init(&data->lock);

	// Allocates one regmap_field per apds9999_reg_fields[] entry
	// pointers are stored in data->regfield[]
	ret = devm_regmap_field_bulk_alloc(&client->dev, data->regmap, data->regfield, apds9999_reg_fields, ARRAY_SIZE(apds9999_reg_fields));
	if (ret) {
		dev_err(&client->dev, "regmap field bulk allocation failed.\n");
		return ret;
	}

	// bring the chip into a known state and enable PS + LS by default
	ret = apds9999_chip_init(data);
	if (ret)
		return ret;

	/*
	 * This registers the irq handlers, if there are any for the platform
	 * the IRQF flags mean the following:
	 *  IRQF_TRIGGER_LOW: level-triggered. fires as long as the line is low. Matches the datasheet
	 *  IRQF_ONESHOT: this keeps the IRQ line masked until the soft-IRQ is handled to prevent spurious fires
	 */
	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						apds9999_irq_handler,
						apds9999_irq_thread,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						APDS9999_DRIVER_NAME, indio_dev);


		if (ret) {
			dev_err(&client->dev, "failed to request IRQ %d: %d\n", client->irq, ret);
			return ret;
		}
	}

	//TODO

	// register the driver for this iio device, return if it fails
	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		return ret;

	dev_info(&client->dev,"Hello world from apds9999");
	return 0;
}


// This function is called when the kernel unloads the driver
// We can release the i2c_client handle
static void apds9999_remove(struct i2c_client *client){
	// retrieve the iio_dev that we stored during probe, then our driver data from it
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct apds9999_data *data = iio_priv(indio_dev);

	// power down both PS and LS on module removal
	regmap_write(data->regmap, APDS9999_REG_MAIN_CTRL, 0x00);

	dev_info(&client->dev,"Goodbye world from apds9999");
}


// This holds the names of the sensors this driver can handle
// The second parameter is an optional driver data value that may distinguish different versions of the same sensor
static const struct i2c_device_id apds9999_idtable[] = {
      { "apds9999", 0 },
      { }
};

MODULE_DEVICE_TABLE(i2c, apds9999_idtable);

static const struct of_device_id apds9999_oftable[] = {
	{ .compatible = "brcm,apds9999" },
	{ }
};

MODULE_DEVICE_TABLE(of, apds9999_oftable);

static struct i2c_driver apds9999_driver = {
      .driver = {
              .name           = APDS9999_DRIVER_NAME,   // this is the drivers name and should match the modules name
              .of_match_table = apds9999_oftable,
      },

      .id_table       = apds9999_idtable,
      .probe          = apds9999_probe,
      .remove         = apds9999_remove,
};

// Device auto detection could be added, but since this is a rather uncommon device, it is advised against in the docs.

module_i2c_driver(apds9999_driver);

MODULE_AUTHOR("Lucx & Lstx");
MODULE_DESCRIPTION("APDS-9999 Digital Proximity and RGB sensor driver");
MODULE_LICENSE("GPL v2");
