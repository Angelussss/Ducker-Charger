

![Project Status](https://img.shields.io/badge/Status-Work_in_Progress-yellow)
![License](https://img.shields.io/github/license/Angelussss/Ducker-Charger)
![KiCad](https://img.shields.io/badge/Designed_with-KiCad-blue)

**THIS PROJECT HAS NOT YET BEEN COMPLETED, TAKE ANY INFO AT YOUR OWN RISK**

**Ducker-Charger** is a completely custom, open-source smart power bank designed from scratch. It pairs an autonomous, hardware-based Battery Management System (BMS) with bidirectional USB-PD charging and a telemetry/HMI core, packed into a compact design.

## ⚠️ Disclaimer & Safety

**READ CAREFULLY BEFORE PROCEEDING**

This project involves the use of Lithium-Ion (Li-Ion) or Lithium Polymer (LiPo) batteries. If handled improperly, these batteries can overheat, catch fire, or explode.
* **I assume no liability** for any damage to property, people, or components resulting from the construction or use of this project.
* Always double-check polarity before connecting the batteries.
* Do not attempt to assemble this circuit if you are not experienced in handling lithium batteries.

## 🌟 Key Features

* **Microcontroller:** STM32F401RBT6 (ARM Cortex-M4)
* **Battery Support:** 12x 18650 cells, 4S3P (Sony Murata VTC5) — 14.4V nominal / 16.8V max / ~115Wh
* **Charger:** TI BQ25713 buck-boost battery charger
* **Outputs:**
    * 2x USB-C (USB-PD, one for recharging)
    * Lab output (exposed pins that behave like a tabletop power supply)
    * 2x USB-A
* **Interface:** Status LEDs, SPI display, pressable rotary encoder
* **Protections:** Overcharge, Overdischarge, Short-circuit, Thermal protection.

## 🧩 Hardware Architecture

Distributed design — safety, power conversion and telemetry are handled by dedicated subsystems.

| Subsystem | Part | Role |
|-----------|------|------|
| Safety / Protection | TI **BQ77915** + N-ch MOSFETs | Autonomous, software-independent cell OV/UV/SC/OC cutoff (4S, always-on) |
| Charger | TI **BQ25713** | Buck-boost battery charger |
| Main USB-C PD | TI **TPS25750** | Primary PD port controller, drives the charger |
| Aux USB-C PD | Infineon **CYPD3175** (CCG3PA) | Secondary PD ports negotiation |
| Fuel gauge | TI **BQ34Z100** | Impedance Track™ State-of-Charge / Time-to-Empty |
| Telemetry | TI **INA3221** | 3-channel voltage/current monitoring |
| Step-down | TI **TPS54302 / TPS54202** | Auxiliary rails |
| ESD / I²C isolation | **USBLC6**, **ISO1540** | Port ESD protection, isolated I²C |
| Host | **STM32F401RBT6** | I²C master, thermal manager, HMI / supervisor |


## 📂 Repository Structure

```text
Ducker-Charger/
├── PCB/                    # KiCad projects (schematics + PCB layouts)
│   ├── Ducker-Charger/     # Main charger board
│   └── Sensing-Board/      # Cell sensing board
├── firmware/               # Source code & CubeMX project
│   ├── cubeMX/
│   └── TPS25750/           # PD controller config
└── docs/                   # Documentation & architecture notes
```

## 🚧 Not Yet Completed

This is an active, in-progress project. 

Take any info here at your own risk until the design is validated.



