# Ducker-Charger: Hardware Architecture

This document details the hardware design, power architecture, and circuit analysis of the Ducker-Charger.


## 1. Power Architecture & Domains

The system utilizes a **Narrow Voltage DC (NVDC)** architecture. This decouples the system load from the battery, allowing the device to operate immediately upon USB-C insertion even if the battery is depleted or removed ("Instant-on").

### Voltage Rails

| Rail Name | Voltage | Source | Description |
| --- | --- | --- | --- |
| **VBUS** | 5V - 20V | USB-C / Boost | Input/Output power rail negotiated by PD Controller.|
| **VSYS** | Regulated | U1 (Charger) | Main system bus. Powered by Buck-Boost from VBUS or Battery.|
| **+BATT** | 10V - 16.8V | Battery Pack | Raw battery stack voltage (4S Li-Ion typical).|
| **+3V3** | 3.3V | **IC2** (TPS54302) | Logic supply for MCU and sensors, derived from `VSYS`.|
| **5V_USB** | 5.0V | **U4, U5** | Regulated output for USB-A ports.|
---

## 2. Subsystem Analysis

### 2.1. High-Power Stage & USB-C PD

*Schematic Sheet: `/High_pow/*`

* **USB-C PD Controller (U2 - TPS25750):** Manages the Power Delivery policy and negotiation on CC lines. It controls the input VBUS MOSFETs via `PP5V` gate drive and communicates with the MCU via `I2C_PD`.


* **Buck-Boost Charger (U1 - BQ25713):** The core power converter. It utilizes a 4-switch H-Bridge topology (MOSFETs Q3, Q4, Q13, **Q5**) to regulate voltage regardless of the VBUS/Battery differential. It manages the Power Path via the `BATDRV` signal, controlling Q15 to charge or discharge the battery.



### 2.2. Battery Management System (BMS)

*Schematic Sheet: `/BMS/*`

The BMS prioritizes safety through hardware redundancy, separating protection from monitoring.

* **Primary Protection (U8 - BQ77915):** A standalone hardware protector. It monitors individual cell voltages (`VC0`-`VC4`) and drives the series protection MOSFETs (Q1, Q2) to cut off the battery in case of Over-Voltage, Under-Voltage, or Short-Circuit. It also handles passive cell balancing.


* **Fuel Gauge (IC3 - BQ34Z100-R2):** Uses a low-side sense resistor (`SRN`/`SRP`) to track State of Charge (SoC).


* **Critical Design Note:** An **ISO1540 (U9)** I2C isolator is used. The Fuel Gauge references the Battery Negative (`-BATT`), while the MCU references System Ground (`GND`). The isolator prevents ground loops and protects the logic when protection FETs are open.





### 2.3. Logic & Control

*Schematic Sheet: `/Logic/*`

* **MCU (U7 - STM32F401RBT6):** The central controller. It monitors system telemetry, drives the display, and configures the PD controller and Charger via I2C.


* **Power Supply:** The MCU regulator (**IC2 - TPS54302**) is powered by `VSYS`. Since `VSYS` is backed by `VBUS`, the logic remains active during charging.



### 2.4. Auxiliary Outputs & Sensing

*Schematic Sheet: `/Low_pow/*`

* **Lab Mode Switching:** The system can route power to banana jacks (`V_LABS`) or USB ports via back-to-back MOSFETs (Q6-Q9), controlled by `USB_C_ZENABLER` and `LAB_ZENABLER` signals.


* **Current Monitoring:** An **INA3221** (3-channel monitor) provides real-time current and voltage telemetry for the auxiliary outputs.



---

## 3. Critical Implementation Details

### Ground Separation

The design features two distinct ground domains:

1. **GND:** System Ground (Logic, Power Stage, USB-C).
2. **-BATT:** Battery Pack Negative.

*Connection:* These are connected via the **Protection MOSFETs** and the **Sense Resistor**. When the protection trips, `-BATT` floats.

