# Hardware Interface & Memory Map: Ducker-Charger Mainboard

**Target MCU:** STM32F401RBT6
---

## 1. Human Interface (Encoder & Button)

The user interface relies on a mechanical rotary encoder with an integrated push button.
**Hardware Note:** Signals are filtered via RC networks on the PCB. Internal pull-ups should be configured for the button if not sufficient.

| Signal Name | STM32 Pin | GPIO Config | Function |
| :--- | :--- | :--- | :--- |
| **ENC_A** | **PC6** | `AF_TIM` (Encoder Mode) | Encoder Phase A |
| **ENC_B** | **PC7** | `AF_TIM` (Encoder Mode) | Encoder Phase B |
| **ENC_BUTT** | **PC0** | `Input` (Pull-up) | Enter Button (Active Low) |

---

## 2. Analog Sensing System (ADC)

All sensors are read via **ADC1** (12-bit resolution).
**Reference (VREF+):** VDDA (+3.3V, filtered).

### 2.1 Channel Mapping
| Signal Name | STM32 Pin | ADC Channel | Description |
| :--- | :--- | :--- | :--- |
| **NTC_1** | **PA0** | IN0 | Temp Zone 1 (General/FETs) |
| **NTC_2** | **PA1** | IN1 | Temp Zone 2 |
| **NTC_3** | **PA2** | IN2 | Temp Zone 3 |
| **NTC_4** | **PA3** | IN3 | Temp Zone 4 |
| **NTC_ONB** | **PA4** | IN4 | On-Board NTC |
| **HP.IADPT** | **PA6** | IN6 | USB-C Input Current |
| **HP.IBAT** | **PA7** | IN7 | Battery Current (Charge/Discharge) |
| **HP.PSYS** | **PC4** | IN14 | Total System Power |

### 2.2 Data Conversion Formulas

**Thermistors (NTC)**
Standard voltage divider with 10k Pull-up to 3.3V.

**Input Current (HP.IADPT)**
Hardware: Sense Resistor = 10 mOhm, BQ25713 Gain = 20x.
```c
// Result in Amperes
float current_in_A = (adc_voltage_mV / 200.0f);

```

**System Power (HP.PSYS)**
Analog voltage proportional to system power (Battery + VBUS).

```c
// Result in Watts (Approximate, depends on R72/R69 ratio)
float power_sys_W = (adc_voltage_V * 60.0f);

```

---

## 3. Digital Communication Architecture

The system uses two separate I2C buses to isolate High Power (PD) negotiation from Low Power (System) telemetry.

### 3.1 High Power Bus (I2C_PD) - "Monitor Mode"

**Peripheral:** I2C3, **Pins:** SCL = **PA8**, SDA = **PC9**

| Device | IC | Address (7-bit) | Firmware Role |
| --- | --- | --- | --- |
| **PD Controller** | **TPS25750** | **0x20** | **Primary Target.** Query for PD Contracts & Status. Length-prefixed host interface, access via `system/tps25750_io.c` only. |
| **Charger** | **BQ25713** | N/A | **Not on this bus.** Slaved to the TPS25750 on their private `I2C_EX`; unreachable from the MCU. Its MCU-visible signals are the analog IADPT/IBAT/PSYS pins (ADC1) and the CHRG_OK / EN_OTG GPIOs. |

### 3.2 Low Power Bus (I2C_LP) - "Control Mode"

**Peripheral:** I2C1, **Pins:** SCL = **PB6**, SDA = **PB7**

| Device | IC | Address (7-bit) | Function & Configuration |
| --- | --- | --- | --- |
| **Power Monitor** | **INA3221** | **0x40** | **USB-A Current.** *Init:* CH1 (USB-A1) + CH2 (USB-A2) enabled; CH3 is tied to GND on this board. *Shunts:* 10 mOhm. |
| **Fuel Gauge** | **BQ34Z100-R2** | **0x55** | **Battery SoC.** Behind the ISO1540 isolator (battery side). Full register map below. |
| **Sec. USB-C PD** | **CYPD3175** (EZ-PD CCG3PA) | **0x08** | **Secondary port PD controller.** HPI protocol, 16-bit register addresses. Negotiates autonomously; MCU reads events/contract. |
| **Sec. USB-C Reg.** | **STPD01PUR** | **0x05** | **Programmable buck for the C2/Lab rail.** `ADD` pin grounded per netlist → 0x05. Set VOUT/ILIM, monitor faults. |

### 3.2.1 BQ34Z100-R2 Register Map (Fuel Gauge, I2C_LP @ 0x55)

These are the standard/extended commands the firmware reads from the fuel gauge.
Addresses, byte order, and units verified against the **BQ34Z100-R2 Technical
Reference Manual (SLUUCO5A)**. Multi-byte values are **little-endian** (low byte at
the lower address). The bundled `docs/bq34z100-r2.pdf` is the *datasheet* and does
**not** contain this table; it is sourced from the TRM.

| Command | Addr (LSB/MSB) | Bytes | Unit | Firmware field (`FuelGaugeSensors`) |
| :--- | :--- | :---: | :--- | :--- |
| `StateOfCharge()`      | 0x02       | 1 | %       | `SoC` |
| `Voltage()`            | 0x08/0x09  | 2 | mV      | `voltage` |
| `AverageCurrent()`     | 0x0A/0x0B  | 2 | mA      | `avgCurrent` |
| `Temperature()`        | 0x0C/0x0D  | 2 | 0.1 K   | `externalTemperature` |
| `Flags()`              | 0x0E/0x0F  | 2 | bitfield| `flags` (see below) |
| `Current()`            | 0x10/0x11  | 2 | mA      | `current` |
| `AverageTimeToEmpty()` | 0x18/0x19  | 2 | minutes | `avgTimeToEmpty` |
| `AverageTimeToFull()`  | 0x1A/0x1B  | 2 | minutes | `avgTimeToFull` |
| `VoltScale()`          | 0x20       | 1 | N/A | `voltageScale` |
| `CurrScale()`          | 0x21       | 1 | N/A | `currentScale` |
| `InternalTemperature()`| 0x2A/0x2B  | 2 | 0.1 K   | `internalTemperature` |
| `CycleCount()`         | 0x2C/0x2D  | 2 | counts  | `cycleCount` |
| `StateOfHealth()`      | 0x2E/0x2F  | 2 | %       | `stateOfHealth` |

**Scaling note (per TRM §2.2):** when `VoltScale()` / `CurrScale()` return a value
> 1, the raw `Voltage()` / `Current()`, `AverageCurrent()`, and capacity readings
must be multiplied by that scale to obtain real units.

**`Flags()` bit definitions (TRM Table 2-6):**

| Byte | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| High | OTC | OTD | BATHI | BATLOW | CHG_INH | XCHG | FC | CHG |
| Low  | REST | RSVD | RSVD | CF | RSVD | SOC1 | SOCF | DSG |

The firmware decodes all high-byte bits and the documented low-byte bits
(`DSG`, `SOCF`, `SOC1`, `CF`, `REST`).

---

## 4. GPIO Control & Interrupts

### 4.1 Digital Outputs (Active High)

Signals used to enable power rails and modes.

| Signal Name | STM32 Pin | Hardware Target | Description |
| --- | --- | --- | --- |
| **USB_A1_CTRL** | **PC1** | Q10 (Load Switch) | Enable USB-A Port 1 Output (5V) |
| **USB_A2_CTRL** | **PC2** | Q11 (Load Switch) | Enable USB-A Port 2 Output (5V) |
| **HP.EN_OTG** | **PB15** | BQ25713 EN_OTG | Enable OTG (Reverse Power) on C1. ANDed by the charger with the I2C bit the TPS25750 sets, the MCU holding this low is an independent kill switch on C1 sourcing. |
| **STPD01_EN** | **PC11** | STPD01 Enable Pin | Enable the C2/Lab buck converter |
| **C2_PORT_EN** | **PA12** | Port FET | Route the STPD01 rail to the USB-C2 connector |
| **C2_LAB_EN** | **PA11** | Lab FET | Route the STPD01 rail to the Lab output (FSM MANUAL state; interlocked with C2_PORT_EN) |
| **BCKL_CTRL** | **PB8** | JP402 → FET | Display backlight (polarity per jumper; drive via `ILI9341_Backlight()` only) |

### 4.2 Interrupts & Status Inputs

Critical signals, polled from `readCS()` each FSM tick (the encoder button
on PC0/EXTI0 is the only line configured as a true interrupt).

| Signal Name | STM32 Pin | Trigger | Action Required |
| --- | --- | --- | --- |
| **HP.PD_IRQ** | **PB14** | Falling Edge | Read TPS25750 Event Register (Plug/Unplug). |
| **C2_ST_INT** | **PC12** | Falling Edge | Read STPD01 fault register (overcurrent/temp). |
| **C2_RDY** | **PC3** | Falling Edge | Read CYPD3175 HPI event register (PD event/fault). |
| **HP.CHRG_OK** | **PB13** | High Level | Adapter is valid. Also the DEEP_SLEEP wake gate: a button wake is only honored while this pin is high. |

---

## 5. Firmware Operational Notes

1. **Boot & Safety Sequence:**
* Check `HP.CHRG_OK` (PB13) before enabling high-current paths.
* `STPD01_EN` (PC11) stays low at boot, `initialization.c` programs the STPD01 with safe defaults (5 V / 3 A, output disabled) and enabling remains the runtime logic's job.
* Initialize `INA3221` (I2C_LP) immediately to provide OCP (Over Current Protection) for USB-A ports.


2. **Fuel Gauge Polarity Warning:**
* The hardware design places the **SRP** pin on the Cell Side and **SRN** on the System Side.
* **Action:** Ensure the BQ34Z100 flash configuration matches this polarity. If configured incorrectly, discharge current will be reported as positive charge.


3. **I2C Bus Separation:**
* **I2C_PD (I2C3: PA8 SCL / PC9 SDA)** is strictly for the TPS25750. Do not attempt to address battery-side or LP devices on this bus.
* **I2C_LP (I2C1: PB6/PB7)** carries everything else: CYPD3175, STPD01, INA3221, and the BQ34Z100 behind the ISO1540 isolator.
