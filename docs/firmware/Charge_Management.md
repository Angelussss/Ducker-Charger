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
- Reading status registers and flags of different key components (`BQ34Z100-R2`, `INA3221`, `TPS25750`, `CYPD3175`, `STPD01`) via two different `I2C` channels
- Monitoring the secondary USB-C PD controller (`CYPD3175`) via HPI, reading the negotiated contract, configuring accordingly the power delivery and enabling/disabling the secondary USB-C port and the power delivery component
- Gating the primary port's OTG (source) mode through `EN_OTG` (PB15), enabled only when a sink device negotiates while the FSM allows outputs, dropped on unplug and in every protection state
- Applying the user-configurable output ceiling on the secondary USB-C port (`setSecondaryUSBC_VoltageCeiling/CurrentCeiling`): the delivered rail is `min(ceiling, negotiated contract)`, the ceiling can only lower what PD negotiated, never raise it
- Providing ready-to-use get functions for easy data fetch regarding sensors, statuses and components read

All TPS25750 register access goes through `system/tps25750_io.c`: the host interface is length-prefixed on the wire (a read returns `[len][data...]`, a write is `[reg][len][data...]`), which `HAL_I2C_Mem_*` cannot produce, hence the dedicated `tps25750_read()`/`tps25750_write()` helpers on I2C3.


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
| `C2_PORT_EN`  | PA12      | Port FET          | Route the STPD01 rail to the USB-C2 connector |
| `C2_LAB_EN`   | PA11      | Lab FET           | Route the STPD01 rail to the Lab output (interlocked with `C2_PORT_EN`) |
| `HP.EN_OTG`   | PB15      | BQ25713 EN_OTG    | Enable OTG (Reverse Power) on C1, see the Primary USB-C section |
| `STPD01_EN`   | PC11      | STPD01 Enable Pin | Enable the shared C2/Lab buck converter |

#### Interrupts & Status Inputs

Critical signals. Configure as EXTI (External Interrupt) or Polling.

| Signal Name  | STM32 Pin | Trigger      | Action Required                                              |
| ------------ | --------- | ------------ | ------------------------------------------------------------ |
| `HP.PD_IRQ`  | PB14      | Falling Edge | Read TPS25750 Event Register (Plug/Unplug).                  |
| `C2_ST_INT`  | PC12      | Falling Edge | Read STPD01 fault register (overcurrent/temp).               |
| `C2_RDY`     | PC3       | Falling Edge | Read CYPD3175 HPI event register (PD protocol event/fault).  |
| `HP.CHRG_OK` | PB13      | High Level   | Adapter is valid. Safe to start charging logic.              |

### I2C1

#### BQ34Z100-R2 Register Map (Fuel Gauge @ 0x55)

These are the standard/extended commands the firmware reads from the fuel gauge.
Addresses, byte order, and units verified against the **BQ34Z100-R2 Technical
Reference Manual (SLUUCO5A)**. Multi-byte values are **little-endian** (low byte at
the lower address). The bundled [`bq34z100-r2.pdf`](../../PCB/Ducker-Charger/docs/bq34z100-r2.pdf) is the *datasheet* and does
**not** contain this table; it is sourced from the [TRM](../../PCB/Ducker-Charger/docs/BQ34Z100-R2_Technical Reference_Manual.pdf). All mentioned registers are Read-Only (RO)

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
| `VoltScale()`           | 0x20           |   1   | N/A | `voltageScale`                      |
| `CurrScale()`           | 0x21           |   1   | N/A | `currentScale`                      |
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
- **SOCF:** State of Charge is below the final (critical) low threshold
- **SOC1:** State of Charge is at threshold 1
- **CF:** Condition Flag, indicates the gauge needs a re-learning cycle. Firmware pushes `EVT_ERROR` on rising edge
- **REST:** Rest condition detected (no charge or discharge current for a sustained period)

*Note:* The firmware decodes all high-byte bits and all four documented low-byte bits (`DSG`, `SOCF`, `SOC1`, `CF`, `REST`). `CF` is the only low-byte flag that pushes an FSM event; the rest are available for the display layer

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

#### CYPD3175-24LQXQ HPI Register Map @ 0x08

The CYPD3175 (Cypress EZ-PD CCG3PA) uses the **HPI (Host Processor Interface)** protocol over I2C. All register addresses are **16-bit** and must be transmitted using `I2C_MEMADD_SIZE_16BIT`. The register space is split between global device registers and per-port registers.

**Global registers:**

| Register Name | Addr   | Access | Bytes | Description                                       |
| :------------ | :----- | :----- | :---: | :------------------------------------------------ |
| `INTR_REG`    | 0x0006 | R/WC   |   1   | Interrupt source flags; write 1 to clear each bit |

- **`INTR_REG` bit description:**

| Bit | Name         | Description                    |
| --- | ------------ | ------------------------------ |
| 0   | `DEV_INTR`   | Device-level interrupt pending |
| 1   | `PORT0_INTR` | Port 0 interrupt pending       |

**Port 0 registers (port base 0x1000, offsets per `cy_hpi_master_port_reg_t`):**

| Register Name         | Addr   | Access | Bytes | Description                                                  |
| :-------------------- | :----- | :----- | :---: | :----------------------------------------------------------- |
| `PORT0_TYPE_C_STATUS` | 0x100C | RO     |   4   | Type-C connection state                                      |
| `PORT0_CURRENT_PDO`   | 0x1010 | RO     |   4   | Active PDO contract (USB PD Fixed PDO format, little-endian) |
| `PORT0_CURRENT_RDO`   | 0x1014 | RO     |   4   | Active RDO from sink (USB PD RDO format, little-endian)      |

- **HPI event codes; read from `RESPONSE_REG` (0x007E) per `cy_hpi_master_response_t`:**

| Code | Firmware macro                   | Description                      |
| ---- | -------------------------------- | -------------------------------- |
| 0x82 | `CYPD3175_EVT_OCP`               | VBUS overcurrent fault           |
| 0x83 | `CYPD3175_EVT_OVP`               | VBUS overvoltage fault           |
| 0x85 | `CYPD3175_EVT_DISCONNECT`        | Type-C disconnection             |
| 0x86 | `CYPD3175_EVT_CONTRACT_COMPLETE` | PD contract negotiation complete |
| 0x8F | `CYPD3175_EVT_HARD_RESET`        | Hard Reset received              |
| 0xB6 | `CYPD3175_EVT_OTP`               | Overtemperature fault            |

*Note:* the firmware reads `INTR_REG` (0x0006) to confirm a port 0 interrupt (bit 1 set), then reads the device-level `RESPONSE_REG` (0x007E) for the single-byte event code. The port 0 interrupt bit is then cleared by writing `0x02` back to `INTR_REG`. PDO/RDO data follows the standard [Fixed-Voltage PDO/RDO bit mappings](#usb-power-delivery-standard) and is decoded identically to the primary port.



####  STPD01PUR Register Map @ 0x05

*Note:* the `ADD` pin is grounded on this PCB (confirmed via schematic netlist); per the STPD01 datasheet's I2C address table, `ADD` tied to GND selects `ADD1,ADD0 = 01`, giving a 7-bit address of `0b0000101` (0x05).

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

*Note:* the firmware decodes all bits except 1 and 4. Bit 6 (`OTW`) is decoded and stored in `stpd01_status.overTemperatureWarning` for the display layer but does **not** block STPD01 enable, only bits 0, 2, 5, and 7 (OVP, SCP, OTP, ILIM) are treated as blocking faults in `checkSTPD01()`

### I2C3

#### TPS25750 Register Map @ 0x20

All access is length-prefixed (see `system/tps25750_io.c`): reads return `[len][data...]`, writes send `[reg][len][data...]`. The byte counts below are payload sizes, excluding the length prefix.

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

In this section, a detailed explanation of the logic adopted behind each of the charging ports is provided. The powerbank exposes four output ports: two USB-A ports (`USB-A1`, `USB-A2`), a primary USB-C port (autonomously negotiated by the `TPS25750`), and a secondary USB-C port (PD negotiated autonomously by the `CYPD3175`, output regulated via the `STPD01`).

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

If both `PlugInsertOrRemoval` and `PowerStatusUpdate` are set, the firmware reads the `POWER_STATUS` register and updates the port's connection state and, if connected, whether the powerbank is acting as source or sink:

- **Sink** (being charged): `EVT_CHARGER_CONNECTED` is pushed and the FSM enters CHARGING.
- **Source** (OTG, powering a device): the firmware raises `EN_OTG` (PB15) via `enable_OTG()`, but only while the FSM is in IDLE or SLEEP, the same auto-enable gate used for the secondary port. Per the BQ25713 TRM, OTG output requires this pin HIGH **and** the `ChargeOption3.EN_OTG` I2C bit the TPS25750 sets on its private bus, an AND condition, so the MCU holding the pin low is an independent kill switch on C1 discharging the pack (protection-state `onEnter` handlers rely on exactly this).
- **Unplug**: `disable_OTG()` runs before `EVT_CHARGER_DISCONNECTED` is pushed.

If the port is connected and either `NewContractAsCons` or `NewContractAsProv` is set, the firmware reads the `ACTIVE_CONTRACT_PDO` register and checks the `PDO Type` field. If the contract is not Fixed-Voltage type, it is rejected; otherwise, the firmware extracts the `Voltage` and `Maximum Current` from the PDO, then reads the `ACTIVE_CONTRACT_RDO` in order to extract the `Operating Current`.

Finally, the firmware clears the bits it has read from `INT_EVENT1` by setting them in the `INT_CLEAR1` register, avoiding re-processing already-handled events.

*Note:* the `PlugInsertOrRemoval` AND `PowerStatusUpdate` gate does not currently handle a `POWER_STATUS` change occurring without a plug event

#### Secondary USB-C

The secondary USB-C port is source-only. PD negotiation is handled autonomously by the `CYPD3175` (EZ-PD CCG3PA). The MCU monitors the `C2_RDY` signal (PC3), which is wired to the CYPD3175's `GPIO_3`/interrupt pin; when it goes low, `secondaryUSBC_ConnectionINT()` is called. `C2_ST_INT` (PC12) is wired to the STPD01's `INT` pin and triggers `stpd01_PowerStateINT()` instead.

The function reads `INTR_REG` (0x0006) to confirm a port 0 interrupt (bit 1), then reads the single-byte event code from `RESPONSE_REG` (0x007E). The port 0 interrupt bit is cleared by writing `0x02` to `INTR_REG`. Events are handled as follows:

1. **OVP / OCP** (codes 0x83 / 0x82): USB-C2 and STPD01 are immediately disabled, the contract is cleared, and `EVT_FAULT_CRITICAL` is pushed.
2. **OTP** (code 0xB6): USB-C2 and STPD01 are disabled, negotiation is cleared, and `EVT_FAULT_OT` is pushed.
3. **Hard Reset** (code 0x8F): STPD01 and USB-C2 are disabled and negotiation is cleared; `isPlugged` stays true because the device is still physically present and the CYPD3175 re-advertises automatically.
4. **Disconnect** (code 0x85): contract and connection state are cleared, both STPD01 and USB-C2 are disabled.
5. **Contract complete** (code 0x86): `PORT0_CURRENT_PDO` (0x1010) and `PORT0_CURRENT_RDO` (0x1014) are read to extract the negotiated `Voltage`, `Maximum Current`, and `Operating Current` using the standard USB PD Fixed PDO/RDO bit mapping. The output is then applied through `secondaryUSBC_ApplyOutput()`, the single path shared with the ceiling setters, so a fresh contract and a live ceiling change can never diverge. It clamps the contract to the user ceiling (`min(ceiling, contract)` on both voltage and current, the CYPD3175 only negotiates; the STPD01 is what physically produces the rail, so the clamp is a real limit), configures the `STPD01` via `setupSTPD01()`, and enables it with a 10 ms stabilization delay, after which `checkSTPD01()` verifies no post-setup fault is active. If healthy, the USB-C2 output is enabled and the contract is marked as negotiated. A state gate applies before any of this: auto-enable only runs while the FSM is in IDLE or SLEEP, everywhere else the event is acked and the contract stored, but the output stays off (MANUAL is excluded too: there the STPD01 rail belongs to the Lab output).

Changing the ceiling from the UI while a device is already connected and negotiated re-runs `secondaryUSBC_ApplyOutput()` immediately, the rail is reprogrammed live, no replug needed. Since the STPD01 offers no VOUT/ILIM readback and its rail carries no shunt (INA3221 ch3 is grounded), `setupSTPD01()` records the register-decoded setpoint, exposed via `getSTPD01_SetpointVoltage()/Current()`, the quantized value the rail actually holds, which telemetry and the UI use as the source of truth for that rail.

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

*Note:* the firmware extracts **Operating Current** (bits 19:10) from every RDO contract. For the secondary USB-C port the CYPD3175 handles PDO negotiation internally; the firmware reads the already-resolved contract directly from `PORT0_CURRENT_PDO` (0x1010) and `PORT0_CURRENT_RDO` (0x1014), no Object Position lookup is needed
