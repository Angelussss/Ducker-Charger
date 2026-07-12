

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

## UI Preview

![UI demo](docs/assets/ui_demo.gif)

## 🧩 Hardware Architecture

Distributed design — safety, power conversion and telemetry are handled by dedicated subsystems.

| Subsystem | Part | Role |
|-----------|------|------|
| Safety / Protection | TI **BQ77915** + N-ch MOSFETs | Autonomous, software-independent cell OV/UV/SC/OC cutoff (4S, always-on) |
| Charger | TI **BQ25713** | Buck-boost NVDC battery charger (driven by the TPS25750 on a private I²C bus) |
| Main USB-C PD | TI **TPS25750** | Primary PD port controller; patch bundle streamed by the MCU at every boot |
| Aux USB-C PD | Infineon **CYPD3175** (CCG3PA) | Secondary PD port negotiation (replaced the EOL STUSB4710) |
| Aux programmable rail | ST **STPD01** | I²C-programmable supply for the secondary USB-C / lab output |
| Fuel gauge | TI **BQ34Z100-R2** | Impedance Track™ State-of-Charge / Time-to-Empty |
| Telemetry | TI **INA3221** | 3-channel voltage/current monitoring |
| Step-down | TI **TPS54302 / TPS54202** | Auxiliary rails |
| ESD / I²C isolation | **USBLC6**, **ISO1540** | Port ESD protection, isolated I²C to the battery-referenced gauge |
| Host | **STM32F401RBT6** | I²C master, power-state FSM, thermal manager, HMI / supervisor |


## 📂 Repository Structure

```text
Ducker-Charger/
├── PCB/                        # KiCad projects (see PCB/README.md)
│   ├── Ducker-Charger/         # Main charger board (schematics, layout, datasheets)
│   └── Sensing-Board/          # Cell sensing board
├── firmware/
│   ├── cubeMX/                 # STM32F401 firmware (FSM, charge mgmt, UI, drivers)
│   ├── TPS25750/               # Primary PD controller config (JSON project + .bin)
│   ├── CYPD3175/               # Secondary PD controller firmware (hex, patches, LED driver)
│   ├── emulator/               # Full-PCB emulator: real firmware vs IC models (SDL)
│   ├── interface-tester/       # Desktop UI simulator (SDL)
│   └── tests/                  # Host-side unit tests (FSM, charge, HPI, math)
├── powerbank_enclosure/        # 3D-printable enclosure (F3D, STEP, STL, renders)
└── docs/                       # Documentation — start from docs/README.md
    ├── hardware/               # Power architecture, pin map / BSP reference
    ├── firmware/               # Architecture, FSM, charge mgmt, boot, UI
    └── assets/                 # UI demo gif
```

## 📖 Documentation

Start from **[`docs/README.md`](docs/README.md)** — full system overview and a
walkthrough of how the firmware works, with links to:

* [Hardware architecture](docs/hardware/Hardware_Architecture.md) and the
  [pin-level BSP reference](docs/hardware/BSP_reference.md)
* [Firmware architecture](docs/firmware/Firmware_Architecture.md),
  the [power-state FSM](docs/firmware/FSM.md) and
  [charge management](docs/firmware/Charge_Management.md)
* [Boot initialization & IC provisioning](docs/firmware/Boot_and_Provisioning.md)
  and the [UI / display reference](docs/firmware/UI_and_Display.md)
* [KiCad projects & block diagram](PCB/README.md)

## 🚧 Not Yet Completed

This is an active, in-progress project. 

Take any info here at your own risk until the design is validated.

## Contributing

Issues, suggestions, and pull requests are momentarily suspended, as this is a university course project.

## License

Released under the [Creative Commons Attribution-NonCommercial 4.0 International
License (CC BY-NC 4.0)](LICENSE). © 2026 Angelo Perotti.

You are free to use, modify, and build upon this work for **non-commercial**
purposes, as long as you **credit the author**. See [NOTICE](NOTICE) for
attribution details.

