# APDS-9999 Kernel Driver

This is a linux kernel driver for the Broadcom APDS-9999 sensor.

The sensor features a proximity sensor (PS) as well as a light sensor (LS). The light sensor can be configured to work as ambient light sensor (ALS) or as full RGB sensor; in both modes it also measures infrared (IR) light.

This driver implements almost all features of the sensor and exposes them to userspace. 

Check the [Datasheet](https://docs.broadcom.com/doc/APDS-9999-DS) for more details.

## Repository structure
This repo is structured as follows:

- `src/`: the kernel driver source code
- `Makefile`: to build the kernel driver. Currently the version of the header files is hardcoded to `6.19.10+deb13-amd64`

- `dt/`: device tree overlay files for a raspberry pi to specify the sensors address and interrupt pin
- `test/`: test scripts to test the sensor's various features with real hardware. Check out the `README` in the directory for more details.

## Driver features

The driver supports three modes of operation: Direct Mode, Triggered Buffer Mode, and Event/Interrupt Mode.

### Direct Mode

For all settings the sensor exposes via registers, the driver allows to configure them through sysfs. 

The following sections sections assumes the sensor is already initialized and the driver is loaded. See section [Installation](#installation) for more details.

We are considering the sysfs directory now. For example `/sys/bus/iio/devices/iio:device0/`. `cd` into it to see the files we are talking about in this section.

#### Settings
##### Main Controls

- `ps_enable`: Enables the proximity sensor.
- `ls_enable`: Enables the light sensor.
- `rgb_mode`: Sets the LS into RGB mode. (ALS reagings will not be available)

- `sai_ps`: *Sleep after Interrupt for PS* 
- `sai_ls`: *Sleep after Interrupt for LS*

*Sleep after Interrupt*: When these settings are set, the proximity sensor returns to standby once an interrupt occurs. 
                          Returning to standby means that the `*_enable` is set to `0` when the measurement is finished.

##### Proximity Sensor Controls 

The sensor works by emitting a Laser Beam from a VCSEL and measuring the reflected light.
**VCSEL** stands for *Vertical Cavity Surface Emitting Laser*.

- `ps_vcsel_freq_khz`: sets the Pulse Modulation frequency in kHz. Allowed values are 60, 70, 80, 90, 100 [kHz].
- `ps_vcsel_curr_ma`: sets the VCSEL current in mA. Allowed values are 10, 25 [mA]. (The sensor has a default value that is not specified in the Datasheet, so it may be usefull to set this always)
- `ps_pulses`: sets the number of pulses that the VCSEL emits. Allowed values are from 0 to 255.
- `ps_reso_bit`: sets the resolution bit depth. Allowed values are from 8, 9, 10, 11 [bits].
- `ps_meas_rate_us`: sets the measurement rate in microseconds. Allowed values are 6250, 12500, 25000, 50000, 100000, 200000, 400000 [us].

To reduce the influence of crosstalk (for example the reflection from some coverglass), the sensor features some cancellation levels for the PS. These should be set during startup. 
The *digital value* is subtracted from the measured PS data allready in the sensor, before it gets spit out. 
By using the *analog value*, a reduction in the dynamic rance of the sensor can be avoided. The digital cancellation can be used nevertheless.

- `in_proximity_calibbias`: sets the digital bias value.
- `in_proximity_calibbias_ana`: sets the analog bias value.

When writing to these settings, the ongoing measurement is stopped and eventually a new one is started.

For all settings the respective `*_available` file is present to read from. It returns the valid values for that setting.

##### Light Sensor Controls

- `ls_reso_bit`: sets the resolution bit depth. Allowed values are from 20, 19, 18, 17, 16, 13 [bits] which correspond to 400, 200, 100, 50, 25, 3.125 [ms] respectively.
- `ls_meas_rate_us`: sets the measurement rate in microseconds. Allowed values are 25, 50, 100, 200, 500, 1000, 2000, 2000 [us]. (The double 2000 is from the datasheet. Idk what that is about)
- `in_illuminance_hardwaregain`: sets the hardware gain for the light sensor. This is valid in all modes. Allowed values are 1, 3, 6, 9, 18.

When writing to these settings, the ongoing measurement is stopped and eventually a new one is started.

For all settings the respective `*_available` file is present to read from. It returns the valid values for that setting.

#### Reading
##### Proximity Sensor Reading

The proximity sensor readings are available only as raw values, since the physical value strongly depends on the settings. Some example graphs can be found in the datasheet at page 13. 
If an object is too close to the sensor, the raw value may overflow, in that case, the raw value will saturate and the overflow flag will be set.

- `in_proximity_raw`: raw proximity sensor reading.
- `in_proximity_overflow`: proximity sensor overflow flag.

##### Light Sensor Reading

The light sensor can be in two different modes, rgb or als. 
In rgb mode the `in_intensity_*` channels are readable, while in als mode the `in_illuminance_*` are available.
The `in_intensity_ir_*` channels are always readable.

For each channel, the raw value can be read as well as the processed value to which the right scale was allready applied. 
The scale that gets applied for the current gain and resolution settings can be read from the `*_scale` files.

- `in_illuminance_raw`: raw light sensor reading in ALS mode.
- `in_illuminance_input`: processed light sensor reading in ALS mode.

- `in_intensity_red_raw`: raw red sensor reading in RGB mode.
- `in_intensity_green_raw`: raw green sensor reading in RGB mode.
- `in_intensity_blue_raw`: raw blue sensor reading in RGB mode.

- `in_intensity_red_input`: processed red sensor reading in RGB mode.
- `in_intensity_green_input`: processed green sensor reading in RGB mode.
- `in_intensity_blue_input`: processed blue sensor reading in RGB mode.

- `in_intensity_ir_raw`: raw infrared sensor reading.
- `in_intensity_ir_input`: processed infrared sensor reading.

- `in_intensity_scale` / `in_illuminance_scale`: scale factor for the light sensor readings. Both files map to the same value. The respective table can be found in the datasheet on page 9.


### Events / Interrupts Mode

The sensor has an interrupt pin that is pulled low when one of the sensors crosses a defined threshold. 
In addition to that, the LS can also be configured to trigger the interrupt when the output variation of consecutive measurements exceeds a defined limit. 

The PS has a *logic mode*, when it is activated, the interrupt pin gets updated after every measurement, instead of latching once it fires. This mode has priority over the LS, so when enabled no LS interrupt can be signaled.

The interrupts are processed by the driver and pushed out as events, which can be read from the respective chrdev.

The following files are available in the `events` sub-directory. For example `/sys/bus/iio/devices/iio:device0/events`.

- `ls_int_sel`: selects on which channel the LS interrupt is triggered. Available values are ir, green, red, blue.
- `ps_logic_mode`: enables the *logic mode* for the PS.

For these settings the respective `*_available` files are present to read from. They return the valid values for that setting.

- `in_proximity_thresh_rising_value`: the interrupt triggers when the rising proximity crosses this value.
- `in_proximity_thresh_falling_value`: the interrupt triggers when the falling proximity crosses this value.
- `in_proximity_thresh_period`: the interrupt triggers when the amount of consecutive measurements exceeding the threshold is equal to this value. Available values are 1 to 16.

- `in_illuminance_thresh_rising_value`: the interrupt triggers when the rising illuminance crosses this value. (has to be in threshold mode)
- `in_illuminance_thresh_falling_value`: the interrupt triggers when the falling illuminance crosses this value. (has to be in threshold mode)
- `in_illuminance_thresh_period`: the interrupt triggers when the amount of consecutive measurements exceeding the threshold is equal to this value. Available values are 1 to 16.

- `in_illuminance_change_value`: the interrupt triggers when the change in illuminance exceeds this value.

To enable the interrupts, the `*_en` files are used. For the LS the two modes are mutually exclusive, when enabling one, the other will be disabled.

- `in_illuminance_change_en`: enables the illuminance change interrupt. Disables the illuminance threshold interrupt.
- `in_illuminance_thresh_en`: enables the illuminance threshold interrupt. Disables the illuminance change interrupt.
- `in_proximity_thresh_en`: enables the proximity threshold interrupt. 

### Triggered Buffer Mode

This modality allows to read a bulk of measurement data from the sensor. Each new measurement is read from the sensor on an external trigger. There is no default trigger configured in the driver, since the sensor doesn't have the feature of data-ready interrupts. Therefore any external trigger, such as sysfs or hardware timer trigger may be used. 

This mode is only available if the module is compiled with it. Since it adds `industrialio-triggered-buffer` as additional dependency, there is the option to exclude it by not setting the `APDS9999_BUFFER` compile flag.

The buffer is filled based on the scan index of the different channels. The scan index can be read from the `*_index` files in the `scan_elements` directory.
For reference, the scan indicies are as follows: 0=ps, 1=red, 2=green, 3=blue, 4=ir, 5=als

As always, rgb and als mode cannot be used simultaneously. Therefore only the following scan masks are available:
- PS only (0)
- LS in ALS mode (ALS + IR)(4+5)
- PS + LS in ALS mode (0+4+5)
- LS in RGB mode (RGB + IR)(1+2+3+4)
- PS + LS in RGB mode (0+1+2+3+4)

To enable the different channels, write `1` to the corresponding `*_en` file in the `scan_elements` directory.
As before, `in_intensity_*` files are used to read the intensity of each channel. IR is always enabled, the others only in rgb mode. 
When in als mode, the `in_illuminance_*` files are used.

If a combination of channels is enabled that does not fall in one of the available scan masks listed above, an error will be returned.

To enable the buffer, a trigger has to be setup and then enabled by writing `1` to the `buffer0/enable` file.

## Installation

### Compilation
To use this driver, the source has to be compiled. The Makefile is there to help with that. As allready mentioned, one may need to adapt the version of the kernel headers being used. 

There is the option to compile the driver without the buffer support by not setting the `APDS9999_BUFFER` compile flag. The Makefile sets it by default, it can be disabled by compiling for the target `no-buffer`. (`make no-buffer`)

### Dependencies

The driver depends on the `regmap-i2c` and `industrialio` kernel modules. If compiled with the buffer support, also the `industrialio-triggered-buffer` module is required.

The modules can be loaded as root using `modprobe`: 
```
modprobe regmap-i2c
modprobe industrialio
modprobe industrialio-triggered-buffer
```

### Sensor Mapping

To tell the kernel that the sensor is connected, on which I2C bus it is and what address it is using, there are two options:
1. Add a new i2c device through sysfs. Here the interrupts/events cannot be used.
2. Add the sensor into the device tree

#### 1. Add a new i2c device through sysfs

This is simply done by writing the sensor's name and address to the `new_device` file in the i2c bus directory.
```
echo apds9999 0x52 | tee /sys/bus/i2c/devices/i2c-1/new_device
```

By doing it like this, the kernel and the driver cannot be told if and what interrupt lines are connected. For that the device tree option has to be used.


#### 2. Add the sensor into the device tree

This strongly depends on the device being used. I tested the driver with a raspberry pi. 
Therefore I created a device tree overlay that is loaded during runtime as needed.

The overlay is located in the `dt/` directory. It is the `apds9999-overlay.dts` file. By compiling it the `apds9999-overlay.dtbo` file will be created. This file can then be loaded at runtime. To do it, the Makefile has a `load` target that will simply execute the command `dtoverlay dt/apds9999-rpi-overlay.dtbo`. It has also a `unload` target, which is strongly discouraged since this may lead to memory-leaks.

### Driver loading

Once the previous steps are done, the compiled driver module can be loaded using `insmod apds9999.ko`.

If everything went fine, a new device will be seen under `ls /sys/bus/iio/devices/`, for example `iio:device0`. In this directory the configuration and measurement files can be found: `ls /sys/bus/iio/devices/iio:device0/`.
