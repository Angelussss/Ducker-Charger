# Charge Management

## Table of Contents

- [Introduction](#introduction)
- [Telemetry](#telemetry)
	- [ADC](#adc)
	- [GPIO](#gpio)
	- [I2C1](#i2c1)
	- [I2C3](#i2c3)
- [Charging Ports](#charging-ports)
	- [USB-As](#usb-as)
	- [Primary USB-C](#primary-usb-c)
	- [Secondary USB-C](#secondary-usb-c)
- [Additional Resources](#additional-resources)

## Introduction

This document covers the implementation of the charge management system of the Ducker Charger (files `charge.h` and `charge.c`). Such system is responsible for:
- Reading all relevant sensors via `ADC` and converting appropriately the read values into correct units
- Initializing, reading and updating the statuses of ports and both critical and non-critical signals via `GPIO`
- Detecting faults (e.g. short circuit, over-temperature, etc...) and throwing interrupts accordingly
- Reading status registers and flags of different key components (`BQ34Z100-R2`, `INA3221`, `TPS25750`, `STUSB4710`, `STPD01`) via two different `I2C` channels
- Assisting the secondary USB-C during negotiation by reading the contract, configuring accordingly the power delivery and enabling/disabling the secondary USB-C port and the power delivery component
- Providing ready-to-use get functions for easy data fetch regarding sensors, statuses and components read


---

## Telemetry

### ADC

All sensors are read via **ADC1** (12-bit resolution) according to the following table:
#### Channel Mapping
| Signal Name | STM32 Pin | ADC Channel | Description                        |
| :---------- | :-------- | :---------- | :--------------------------------- |
| `NTC_1`     | PA0       | IN0         | Temp Zone 1 (General/FETs)         |
| `NTC_2`     | PA1       | IN1         | Temp Zone 2                        |
| `NTC_3`     | PA2       | IN2         | Temp Zone 3                        |
| `NTC_4`     | PA3       | IN3         | Temp Zone 4                        |
| `NTC_ONB`   | PA4       | IN4         | On-Board NTC                       |
| `HP.IADPT`  | PA6       | IN6         | USB-C Input Current                |
| `HP.IBAT`   | PA7       | IN7         | Battery Current (Charge/Discharge) |
| `HP.PSYS`   | PC4       | IN14        | Total System Power                 |

*Note:* HP.IBAT is not memorized since the charge/discharge battery current is also fetched via I2C from the Fuel Gauge (`BQ34Z100-R2`). We need to read it anyway to continue the ADC scan sequence.

#### Data Conversion
Data fetched via ADC are provided in voltage and therefore need to be converted accordingly to get their correct value expressed in their proper unit measure.
First of all we need to adjust the fetched, raw voltage measure, according to our hardware specs; the function `toVoltage` performs this transformation by dividing the raw value by `VMAX` and multiplying it by `VREF` as shown:

$$
\frac{V_{raw}}{V_{MAX}} \cdot V_{REF}
$$
where: `VMAX` is equal to 2^nbits - 1 = 4095 (the ADC channel has 12-bit resolution); and `VREF` is equal to `VDD` = 3300mV.

Then we need to convert the obtained voltage value (expressed in `mV`) accordingly to the sensor we are reading:
- **Temperature:** obtained via NTC thermistors, in order to get the value expressed in ºC we need to:
	- Convert from voltage to resistance by doing:
$$
R_{PULLUP} \cdot \frac{V_{out}}{(V_{REF} - V_{out})}
$$
	- Convert from resistance to K using the Beta equation:
$$
T = \left( \frac{1}{T_0} + \frac{1}{\beta} \cdot \ln\left(\frac{R}{R_0}\right) \right)^{-1}
$$
	- Convert from K to ºC:
$$
T_{ºC} = T - 273.15
$$
- **USB-C Input Current:** given the Sense Resistor of 10 mOhm and the `BQ25713` Gain = 20x, in order to get the current value expressed in `mA` we need to divide the voltage by 0.2, or, more specifically:
$$
I = \frac{V_{out}}{R \cdot Gain} \cdot 1000
$$
- **Total System Power:**
$$
P = \frac{V_{out}}{1000} \cdot 60
$$
	- Where 60 is a hardware-derived gain constant (R72/R69 ratio)

### GPIO

#### Digital Outputs (Active High)

Signals used to enable power rails and modes.

| Signal Name   | STM32 Pin | Hardware Target   | Description                           |
| ------------- | --------- | ----------------- | ------------------------------------- |
| `USB_A1_CTRL` | PC1       | Q10 (Load Switch) | Enable USB-A Port 1 Output (5V)       |
| `USB_A2_CTRL` | PC2       | Q11 (Load Switch) | Enable USB-A Port 2 Output (5V)       |
| `USB_C2_CTRL` | PA12      | *(To be defined)* | Enable Secondary USB-C Port Output    |
| `HP.EN_OTG`   | PB15      | U1/U2             | Enable OTG (Reverse Power) on HP Port |
| `LP_ST_EN`    | PC11      | U3 (Enable Pin)   | Enable Aux Converter (V_OUT_AUX)      |

#### Interrupts & Status Inputs

Critical signals. Configure as EXTI (External Interrupt) or Polling.

| Signal Name  | STM32 Pin | Trigger      | Action Required                                 |
| ------------ | --------- | ------------ | ----------------------------------------------- |
| `HP.PD_IRQ`  | PB14      | Falling Edge | Read TPS25750 Event Register (Plug/Unplug).     |
| `LP_ST_INT`  | PC12      | Level/Edge   | Handle Aux Converter Fault (Overcurrent/Temp).  |
| `HP.CHRG_OK` | PB13      | High Level   | Adapter is valid. Safe to start charging logic. |

### I2C1

#### BQ34Z100-R2 Register Map (Fuel Gauge @ 0x55)

These are the standard/extended commands the firmware reads from the fuel gauge.
Addresses, byte order, and units verified against the **BQ34Z100-R2 Technical
Reference Manual (SLUUCO5A)**. Multi-byte values are **little-endian** (low byte at
the lower address). The bundled [`bq34z100-r2.pdf`](../PCB/Ducker-Charger/docs/bq34z100-r2.pdf) is the *datasheet* and does
**not** contain this table — it is sourced from the [TRM](../PCB/Ducker-Charger/docs/BQ34Z100-R2_Technical Reference_Manual.pdf). All mentioned registers are Read-Only (RO)

| Command                 | Addr (LSB/MSB) | Bytes | Unit     | Firmware field (`FuelGaugeSensors`) |
| :---------------------- | :------------- | :---: | :------- | :---------------------------------- |
| `StateOfCharge()`       | 0x02           |   1   | %        | `SoC`                               |
| `Voltage()`             | 0x08/0x09      |   2   | mV       | `voltage`                           |
| `AverageCurrent()`      | 0x0A/0x0B      |   2   | mA       | `avgCurrent`                        |
| `Temperature()`         | 0x0C/0x0D      |   2   | 0.1 K    | `externalTemperature`               |
| `Flags()`               | 0x0E/0x0F      |   2   | bitfield | `flags` (see below)                 |
| `Current()`             | 0x10/0x11      |   2   | mA       | `current`                           |
| `AverageTimeToEmpty()`  | 0x18/0x19      |   2   | minutes  | `avgTimeToEmpty`                    |
| `AverageTimeToFull()`   | 0x1A/0x1B      |   2   | minutes  | `avgTimeToFull`                     |
| `VoltScale()`           | 0x20           |   1   | —        | `voltageScale`                      |
| `CurrScale()`           | 0x21           |   1   | —        | `currentScale`                      |
| `InternalTemperature()` | 0x2A/0x2B      |   2   | 0.1 K    | `internalTemperature`               |
| `CycleCount()`          | 0x2C/0x2D      |   2   | counts   | `cycleCount`                        |
| `StateOfHealth()`       | 0x2E/0x2F      |   2   | %        | `stateOfHealth`                     |

> **Scaling note (per TRM §2.2):** when `VoltScale()` / `CurrScale()` return a value greater than 1, the raw `Voltage()` / `Current()`, `AverageCurrent()`, and capacity readings must be multiplied by that scale to obtain real units

**`Flags()` bit definitions (TRM Table 2-6):**

| Byte | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| High | OTC | OTD | BATHI | BATLOW | CHG_INH | XCHG | FC | CHG |
| Low  | REST | RSVD | RSVD | CF | RSVD | SOC1 | SOCF | DSG |
**Flags Legend: RSVD = Reserved**
- **OTC:** Overtemperature in Charge condition is detected. True when set
- **OTD:** Overtemperature in Discharge condition is detected. True when set
- **BATHI:** Battery High bit that indicates a high battery voltage condition. True when set
- **BATLOW:** Battery Low bit that indicates a low battery voltage condition. True when set
- **CHG_INH:** Charge Inhibit: unable to begin charging. Refer to the data flash (Charge Inhibit Temp Low, Charge Inhibit Temp High) parameters for threshold settings. True when set
- **XCHG:** Charging not allowed
- **FC:** Full charge is detected
- **CHG:** (Fast) charging allowed. True when set  
- **DSG:** Discharging detected. True when set

*Note:* The firmware decodes all high-byte bits and `DSG` from the low byte

#### INA3221 Register Map @ 0x40

| Register Name             | Addr (LSB/MSB) | Access | Bytes | Description                                                                |
| :------------------------ | :------------- | :----- | :---: | :------------------------------------------------------------------------- |
| `Channel-1 Shunt Voltage` | 0x01           | R      |   2   | Averaged shunt voltage value                                               |
| `Channel-2 Shunt Voltage` | 0x03           | R      |   2   | Averaged shunt voltage value                                               |
| `Channel-3 Shunt Voltage` | 0x05           | R      |   2   | Averaged shunt voltage value                                               |
| `Mask/Enable`             | 0x0F           | R/W    |   2   | Alert configuration, alert status indication, summation control and status |

*Note:* All data bytes are transmitted MSB first

**Data conversion:** in order to get the value of current (expressed in mA) that is passing through each channel, we need to convert the average shunt voltage value according to the following formula:

$$
I = \frac{V_{shunt} \cdot 0.04}{0.01}
$$

Where:
- 40 µV (= 0.04 mV) is the unit in which the shunt voltage is stored in the registers
- 10 mΩ is the resistance value for all shunt resistors

- **`Shunt Voltage` registers bit definitions (Starting from Table 6):**

| Bit  | Name       | Access | Description                                                                            |
| :--: | :--------- | :----- | :------------------------------------------------------------------------------------- |
|  15  | `SIGN`     | R      | Sign bit.  <br>0 = positive number  <br>1 = negative number in two's complement format |
| 14-3 | `SD11-0`   | R      | Channel's shunt-voltage data bits                                                      |
| 2-0  | `Reserved` | R      | Reserved                                                                               |

*Note:* the firmware right-shifts the 16-bit read by 3 positions to discard the reserved bits and isolate the signed 12-bit value

- **`Mask/Enable` bit definitions (Table 20):** (only those used by the firmware are specified)

| Bit | Name  | Access | Description                   |
| :-: | :---- | :----- | :---------------------------- |
|  9  | `CF1` | R/W    | Critical-alert flag indicator |
|  8  | `CF2` | R/W    | Critical-alert flag indicator |
|  7  | `CF3` | R/W    | Critical-alert flag indicator |

*Note:* These bits are asserted if the corresponding channel measurement has exceeded the critical alert limit, resulting in the Critical alert pin being asserted

#### STUSB4710 Register Map @ 0x28

| Register Name          | Addr (LSB/MSB) | Access | Bytes | Description                                   |
| :--------------------- | :------------- | :----- | :---: | :-------------------------------------------- |
| `ALERT_STATUS`         | 0x0B           | RC     |   1   | Alert register linked to transition registers |
| `CC_CONNECTION_STATUS` | 0x0E           | RO     |   1   | CC connection status                          |
| `VBUS_ENABLE_STATUS`   | 0x27           | RO     |   1   | VBUS power path activation status             |
| `SRC_PDO1`             | 0x71           | R/W    |   4   | PDO1 capabilities configuration               |
| `SRC_PDO2`             | 0x75           | R/W    |   4   | PDO2 capabilities configuration               |
| `SRC_PDO3`             | 0x79           | R/W    |   4   | PDO3 capabilities configuration               |
| `SRC_PDO4`             | 0x7D           | R/W    |   4   | PDO4 capabilities configuration               |
| `SRC_PDO5`             | 0x81           | R/W    |   4   | PDO5 capabilities configuration               |
| `SRC_RDO`              | 0x91           | RO     |   4   | PDO request status                            |

***Important Note:*** due to insufficient details related to single register bit composition presented in the `STUSB4710` datasheet, a cross-reference with the bit mapping provided by the `STUSB4700` datasheet has been done. This was possible thanks to the fact that the `STUSB4710` contains a subset of the registers present in the `STUSB4700`, as stated by the ST's central support [here](https://community.st.com/interface-and-connectivity-ics-52/stusb4700-4710-i2c-register-details-43666?postid=183084#post183084)

- **`ALERT_STATUS` register description:**

| Bit   | Name                         |
| ----- | ---------------------------- |
| 7     | `HARD_RESET_AL`              |
| 6     | `PORT_STATUS_AL`             |
| 5     | `TYPEC_MONITORING_STATUS_AL` |
| 4     | `CC_HW_FAULT_STATUS_AL`      |
| 3...2 | `Reserved`                   |
| 1     | `PRT_STATUS_AL`              |
| 0     | `PHY_STATUS_AL`              |

*Note:* the firmware decodes only the 6th bit

- **`CC_CONNECTION_STATUS` register description:**

| Bit   | Name                | Description                                                                                                                                                                                                                                                                                                                        |
| ----- | ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 7...5 | `ATTACHED_DEVICE`   | 000: (NONE_ATT) No device connected<br><br>001: (SNK_ATT) Sink device connected<br><br>010: (SRC_ATT) Source device connected<br><br>011: (DBG_ATT) Debug accessory device connected<br><br>100: (AUD_ATT) Audio accessory device connected<br><br>101: (POW_ACC_ATT) Powered accessory device connected<br><br>Others: Do not use |
| 4     | `LOW_POWER_STANDBY` | 0: (LP_OFF) Device is operating in normal mode<br><br>1: (LP_ON) Device is operating in standby mode                                                                                                                                                                                                                               |
| 3     | `POWER_MODE`        | 0: (POW_SNK)<br><br>1: (POW_SRC)                                                                                                                                                                                                                                                                                                   |
| 2     | `DATA_MODE`         | 0: (UFP)<br><br>1: (DFP)                                                                                                                                                                                                                                                                                                           |
| 1     | `VCONN_MODE`        | 0: (VCONN_OFF) VCONN is not supplied<br><br>1: (VCONN_ON) VCONN is supplied                                                                                                                                                                                                                                                        |
| 0     | `ATTACH`            | 0: (UNATTACHED)<br><br>1: (ATTACHED)                                                                                                                                                                                                                                                                                               |

*Note:* the firmware decodes only bits 7-5 in order to detect the connection status of the secondary USB-C port


- **`VBUS_ENABLE_STATUS` register description:**

| Bit   | Name             | Description                                                                                                                               |
| ----- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| 7...2 | `Reserved`       | Reserved                                                                                                                                  |
| 1     | `SINK_VBUS_EN`   | 0: (VBUS_EN_SNK_FORCE_DIS) Disable the forced VBUS_EN_SNK pin assertion<br><br>1: (VBUS_EN_SNK_FORCE) Force the VBUS EN SNK pin assertion |
| 0     | `SOURCE_VBUS_EN` | 0: (VBUS_EN_SRC_FORCE_DIS) Disable the forced VBUS_EN_SRC pin assertion<br><br>1: (VBUS_EN_SRC_FORCE) Force the VBUS EN SRC pin assertion |

*Note:* the firmware only decodes bit 0 (referred to as `VBUS_EN_SRC` in `charge.c`) since the secondary USB-C port is source-only

- **`SRC_PDO(1-5)` & `SRC_RDO` register description:**
	- *Note:* `SRC_PDOx` and `SRC_RDO` each span 4 consecutive byte-addressable registers, forming the 32-bit value described in the [Fixed-Voltage PDO/RDO bit mappings](#usb-power-delivery-standard)

####  STPD01PUR Register Map @ 0x54 (to check)

| Register Name    | Addr (LSB/MSB) | Access | Bytes | Description                                     |
| :--------------- | :------------- | :----- | :---: | :---------------------------------------------- |
| `VOUT`           | 0x00           | R/W    |   1   | Set the output voltage configuration            |
| `ILIM`           | 0x01           | R/W    |   1   | Set the output current limitation configuration |
| `INT_STAT`       | 0x02           | R      |   1   | Interrupt management (see below)                |
| `DIGITAL_ENABLE` | 0x06           | R/W    |   1   | Digital enable                                  |

**Interrupt register description (Table 14):**

| Bit | Function                           | Notes                       |
| --- | ---------------------------------- | --------------------------- |
| 0   | `Overvoltage protection`           |                             |
| 1   | `Constant current function`        |                             |
| 2   | `Short-circuit protection`         |                             |
| 3   | `Power`                            |                             |
| 4   | `Watchdog`                         |                             |
| 5   | `Overtemperature protection`       | Junction temperature 165 °C |
| 6   | `Overtemperature warning`          | Junction temperature 145 °C |
| 7   | `Inductor peak current protection` |                             |

*Note:* the firmware decodes all bits except numbers 1 and 4

### I2C3

#### TPS25750 Register Map @ 0x20 / 0x21 (to check)

| Register Name         | Addr (LSB/MSB) | Access | Bytes | Description                                                                                                                                     |
| :-------------------- | :------------- | :----- | :---: | :---------------------------------------------------------------------------------------------------------------------------------------------- |
| `INT_EVENT1`          | 0x14           | RO     |  11   | Interrupt event bit field for `I2Cs_IRQ`. If any bit in this register is 1, then the `I2Cs_IRQ` pin is pulled low                               |
| `INT_CLEAR1`          | 0x18           | RW     |  11   | Interrupt clear bit field for `INT_EVENT1`. Bits set  <br>in this register are cleared from `INT_EVENT1`                                        |
| `POWER_STATUS`        | 0x3F           | RO     |   2   | Details about the power of the connection                                                                                                       |
| `ACTIVE_CONTRACT_PDO` | 0x34           | RO     |   6   | Power data object for active contract. This register stores `PDO` data for the current explicit USB PD contract, or all zeroes if no contract   |
| `ACTIVE_CONTRACT_RDO` | 0x35           | RO     |   4   | Power data object for the active contract. This register stores the `RDO` of the current explicit USB PD contract, or all zeroes if no contract |

*Note:* `PDO` = Power Data Object; `RDO` = Request Data Object

- **`EVENT1` interrupt register description:**

| Bit | Name                  | Firmware field        | Description                                                                        |
| --- | --------------------- | --------------------- | ---------------------------------------------------------------------------------- |
| 3   | `PlugInsertOrRemoval` | `plugInsertOrRemoval` | USB Plug Status has Changed                                                        |
| 12  | `NewContractAsCons`   | `newContractAsCons`   | Far-end source has accepted an RDO sent by the PD Controller as a Sink             |
| 13  | `NewContractAsProv`   | `newContractAsProv`   | An RDO from the far-end device has been accepted and the PD Controller is a Source |
| 24  | `PowerStatusUpdate`   | `powerStatusUpdate`   | Set whenever contents of `POWER_STATUS` register (0x3F) change                     |

- **`POWER_STATUS` register description:**

| Bit | Name              | Firmware field    | Description                                    |
| --- | ----------------- | ----------------- | ---------------------------------------------- |
| 0   | `PowerConnection` | `powerConnection` | Asserted if there is a connection              |
| 1   | `SourceSink`      | `sourceSink`      | Source / Sink indicator (0 = source; 1 = sink) |

- **`ACTIVE_CONTRACT_PDO` register description:**

| Bytes | Bits  | Name                  | Description                                                              |
| ----- | ----- | --------------------- | ------------------------------------------------------------------------ |
| 5-6   | 15:10 | `Reserved`            | Reserved                                                                 |
| 5-6   | 9:0   | `firstPDOControlBits` | Contains bits 29:20 of the first PDO                                     |
| 1-4   | 31:0  | `ActivePDO`           | Power data object. This field contains the contents of the PDO Requested |

*Note:* the firmware extracts bits of 1-4 bytes; which contain the actual PDO contract (treated as 32-bit little endian value). The PDO contract follows the [USB Power Delivery Standard](#usb-power-delivery-standard), according to which the firmware extracts the PDO type (`pdoType`), the voltage, (`voltageRaw`) and the maximum current (`maxCurrentRaw`), the latter two are then converted respectively in `mV` and `mA`

- **`ACTIVE_CONTRACT_RDO` register description:**
*Note:* the firmware extracts bits of 1-4 bytes; which contain the actual RDO contract (treated as 32-bit little endian value). The RDO contract follows the [USB Power Delivery Standard](#usb-power-delivery-standard), according to which the firmware extracts the operating current (`operatingCurrentRaw`) and converts it into `mA`

---

## Charging Ports

In this section, a detailed explanation of the logic adopted behind each of the charging ports is provided. The powerbank exposes four output ports: two USB-A ports (`USB-A1`, `USB-A2`), a primary USB-C port (autonomously negotiated by the `TPS25750`), and a secondary USB-C port (negotiated by the `STUSB4710` with output regulated via the `STPD01`).

All the following logic and the firmware in general expects:
- The call of its `init` function at start, and
- The periodical call of its functions regarding: (i) sensor reading and update, (ii) components status reading and update and, (iii) critical and non-critical signals status reading and update

### USB-As

Each USB-A port provides a fixed 5V output and therefore requires no negotiation procedure. Current delivered by each port is monitored by the `INA3221`, whose reading function computes the current flowing through each channel and checks for critical-alert conditions, triggering the appropriate fault handling when needed. Both ports can be enabled/disabled by calling their respective functions `enable_USBAx()` and `disable_USBAx()`

#### Primary USB-C

The primary USB-C allows for both source and sink connections. This port is also responsible for charging the powerbank. The firmware periodically reads the `PD_IRQ` signal status in order to detect connection changes. Once it detects one, the firmware fetches from the `INT_EVENT1` the possible reasons the interrupt has been fired:
- `PlugInsertOrRemoval` detects if the USB port status has changed
- `PowerStatusUpdate` assesses if the `POWER_STATUS` register has changed
- `NewContractAsCons` detects a new contract negotiated with our device acting as **sink** (consumer)
- `NewContractAsProv` detects a new contract negotiated with our device acting as **source** (provider)

If both `PlugInsertOrRemoval` and `PowerStatusUpdate` are set, the firmware reads the `POWER_STATUS` register and updates the port's connection state and, if connected, whether the powerbank is acting as source or sink.

If the port is connected and either `NewContractAsCons` or `NewContractAsProv` is set, the firmware reads the `ACTIVE_CONTRACT_PDO` register and checks the `PDO Type` field. If the contract is not Fixed-Voltage type, it is rejected; otherwise, the firmware extracts the `Voltage` and `Maximum Current` from the PDO, then reads the `ACTIVE_CONTRACT_RDO` in order to extract the `Operating Current`.

Finally, the firmware clears the bits it has read from `INT_EVENT1` by setting them in the `INT_CLEAR1` register, avoiding re-processing already-handled events.

*Note:* the `PlugInsertOrRemoval` AND `PowerStatusUpdate` gate does not currently handle a `POWER_STATUS` change occurring without a plug event

#### Secondary USB-C

The secondary USB-C port is source-only: it can only charge connected devices but cannot be charged (sink behavior is not supported). The firmware periodically reads the `STUSB4710`'s `ALERT_STATUS` register in order to detect connection changes, then checks `CC_CONNECTION_STATUS` to determine the attached device type, rejecting the connection if a source is detected.

Once a sink device has been connected, the firmware periodically reads the `SRC_RDO` register. If a contract has been negotiated, the RDO's `Object Position` field identifies which of the 5 PDO configurations has been selected, and the `Operating Current` is extracted from the same read.

The corresponding PDO's `Voltage` and `Maximum Current` are then used to configure voltage and current limits on the `STPD01`.

If configuration succeeds, the `STPD01` is enabled and given a short stabilization delay, after which the firmware checks for post-setup faults via `checkSTPD01()`. If no fault is detected, it verifies the power path is active by reading `VBUS_ENABLE_STATUS`, and only then enables the secondary USB-C port.

---

## Additional Resources

### USB Power Delivery Standard
PDO and RDO contracts follow the USB Power Delivery Standard available [here](https://www.usb.org/documents)

In this section we provide the complete PDO and RDO register bit mappings used for this project and taken from the official documentation provided in the link above.

#### `Fixed-Voltage PDO` register description

| Bit(s)  | Name                                    | Description                                                                                                                                                                                                                                                                                                      |
| ------- | --------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 31...30 | `PDO Type`                              | 00b - Fixed Voltage  <br>Other values are used to describe other PDOs.                                                                                                                                                                                                                                           |
| 29      | `Dual-Role Power`                       | 0b - Port cannot change Power Role via the PR_Swap Message process<br><br>1b - Port may change Power Role via the PR_Swap Message process                                                                                                                                                                        |
| 28      | `USB Suspend Supported`                 | 0b - Sink Shall Not apply the [USB2], [USB3] or [USB4] rules for suspend and May continue to draw the Negotiated power<br><br>1b - Sink Shall follow the [USB2], [USB3] or [USB4] rules for suspend and resume                                                                                                   |
| 27      | `Unconstrained Power`                   | 0b - The Source is limiting its available output power based on its own internal power usage.<br><br>1b - An external Source of power is available that is sufficient to adequately power the Source system while charging external devices, or when the Source's primary function is to charge external devices |
| 26      | `USB Communications Capable`            | 0b - Port is not capable of communication over the USB data lines<br><br>1b - Port is capable of communication over the USB data lines                                                                                                                                                                           |
| 25      | `Dual-Role Data`                        | 0b - Port cannot change Data Role via the DR_Swap Message process<br><br>1b - Port may change Data Role via the DR_Swap Message process                                                                                                                                                                          |
| 24      | `Unchunked Extended Messages Supported` | 0b - Port does not support Unchunked Extended Message<br><br>1b - Port supports both Chunked and Unchunked Extended Message                                                                                                                                                                                      |
| 23      | `EPR Capable`                           | 0b - Source will only provide power Capabilities in the Standard Power Range<br><br>1b - Source is capable of providing power Capabilities in the Extended Power Range. The Source may enter EPR Mode when requested by an EPR Capable Sink                                                                      |
| 22      | `Reserved`                              | **Reserved**, receiver **Shall** ignore this field                                                                                                                                                                                                                                                               |
| 21...20 | `Peak Current`                          | Peak Current                                                                                                                                                                                                                                                                                                     |
| 19...10 | `Voltage`                               | Voltage, **in 50mV units**                                                                                                                                                                                                                                                                                       |
| 9...0   | `Maximum Current`                       | Maximum amount of current supported at this voltage, **in 10mA units**                                                                                                                                                                                                                                           |

*Note:* the firmware always extracts the **Voltage** (bits 19:10) and **Maximum Current** (bits 9:0) fields from each PDO. For the primary USB-C port, it additionally extracts the **PDO Type** to verify the contract is Fixed-Voltage type before proceeding; the secondary USB-C port assumes PDOs configured as Fixed-Voltage type only. Bits 29:20 (flags, peak current) are not currently read or used by `charge.c`

#### `Fixed-Voltage RDO` register description

| Bit(s)  | Name                                    | Description                                                                                                                |
| ------- | --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 31...28 | `Object Position`                       | Indicates the corresponding PDO configuration location                                                                     |
| 27      | `Giveback`                              | **Deprecated**, receiver **Shall** ignore this field.                                                                      |
| 26      | `Capability Mismatch`                   |                                                                                                                            |
| 25      | `USB Communications Cable`              |                                                                                                                            |
| 24      | `No USB Suspend`                        |                                                                                                                            |
| 23      | `Unchunked Extended Messages Supported` |                                                                                                                            |
| 22      | `EPR Capable`                           |                                                                                                                            |
| 21...20 | `Reserved`                              | **Reserved**, receiver **Shall** ignore this field.                                                                        |
| 19...10 | `Operating Current`                     | Operating current, **in 10mA units**<br><br>Indicates the highest current the Sink will draw during the Explicit Contract. |
| 9...0   | `Maximum Operating Current`             | **Deprecated**, transmitter **Shall** set this field equal to "Operating Current", receiver should ignore this field.      |

*Note:* the firmware extracts **Operating Current** (bits 19:10) from every RDO contract. For the secondary USB-C, it additionally extracts the PDO **Object Position** using bits 30:28 (3 bits) rather than the full 31:28 field. This is sufficient since object positions only range from 1 to 7
