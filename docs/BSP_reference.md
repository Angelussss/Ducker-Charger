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

**Peripheral:** I2C3 — **Pins:** SCL = **PA8**, SDA = **PC9**

| Device | IC | Address (7-bit) | Firmware Role |
| --- | --- | --- | --- |
| **PD Controller** | **TPS25750** | **0x20** (or 0x21) | **Primary Target.** Query for PD Contracts & Status. |
| **Charger** | **BQ25713** | **0x6B** | **Secondary.** Slave to TPS. Monitor only if direct charger access is needed. |

### 3.2 Low Power Bus (I2C_LP) - "Control Mode"

**Peripheral:** I2C1 — **Pins:** SCL = **PB6**, SDA = **PB7**

| Device | IC | Address (7-bit) | Function & Configuration |
| --- | --- | --- | --- |
| **Power Monitor** | **INA3221** | **0x40** | **USB-A & Aux Current.**<br>

<br> *Init:* Enable CH1 (USB-A1), CH2 (USB-A2), CH3 (Aux). <br>

<br> *Shunts:* All 10 mOhm. |
| **Fuel Gauge** | **BQ34Z100-R2** | **0x55** | **Battery SoC.**<br>

<br> Read `Voltage()`, `Current()`, `StateOfCharge()`. |
| **Sec. USB-C** | **STUSB4710** | **0x28** | **Low Power Port.**<br>

<br> Read attachment status. |
| **Aux Reg.** | **STPD01PUR** | **0x54** (Verify) | **V_OUT_AUX Control.**<br>

<br> Set voltage and monitor Faults. |

---

## 4. GPIO Control & Interrupts

### 4.1 Digital Outputs (Active High)

Signals used to enable power rails and modes.

| Signal Name | STM32 Pin | Hardware Target | Description |
| --- | --- | --- | --- |
| **USB_A1_CTRL** | **PC1** | Q10 (Load Switch) | Enable USB-A Port 1 Output (5V) |
| **USB_A2_CTRL** | **PC2** | Q11 (Load Switch) | Enable USB-A Port 2 Output (5V) |
| **HP.EN_OTG** | **PB15** | U1/U2 | Enable OTG (Reverse Power) on HP Port |
| **LP_ST_EN** | **PC11** | U3 (Enable Pin) | Enable Aux Converter (V_OUT_AUX) |

### 4.2 Interrupts & Status Inputs

Critical signals. Configure as EXTI (External Interrupt) or Polling.

| Signal Name | STM32 Pin | Trigger | Action Required |
| --- | --- | --- | --- |
| **HP.PD_IRQ** | **PB14** | Falling Edge | Read TPS25750 Event Register (Plug/Unplug). |
| **LP_ST_INT** | **PC12** | Level/Edge | Handle Aux Converter Fault (Overcurrent/Temp). |
| **HP.CHRG_OK** | **PB13** | High Level | Adapter is valid. Safe to start charging logic. |

---

## 5. Firmware Operational Notes

1. **Boot & Safety Sequence:**
* Check `HP.CHRG_OK` (PB13) before enabling high-current paths.
* Enable `LP_ST_EN` (PC11) early to power internal peripherals if needed.
* Initialize `INA3221` (I2C_LP) immediately to provide OCP (Over Current Protection) for USB-A ports.


2. **Fuel Gauge Polarity Warning:**
* The hardware design places the **SRP** pin on the Cell Side and **SRN** on the System Side.
* **Action:** Ensure the BQ34Z100 flash configuration matches this polarity. If configured incorrectly, discharge current will be reported as positive charge.


3. **I2C Bus Separation:**
* **I2C_PD (PB8/9)** is strictly for the High Power PD Controller (TPS) and Charger (BQ). Do not attempt to address the BMS or INA3221 on this bus.
* **I2C_LP (PB6/7)** is for all other system telemetry (BMS, Aux, USB-A sensing).



```

```
