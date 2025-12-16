# 🦆 Ducker-Charger

![Project Status](https://img.shields.io/badge/Status-Work_in_Progress-yellow)
![License](https://img.shields.io/github/license/Angelussss/Ducker-Charger)
![KiCad](https://img.shields.io/badge/Designed_with-KiCad-blue)

**Ducker-Charger** is a completely custom, open-source power bank designed from scratch. This project aims to create a reliable Battery Management System (BMS) and charging solution, packed into a compact design.

![Ducker Charger Render](img/render_preview.png)

## ⚠️ Disclaimer & Safety

**READ CAREFULLY BEFORE PROCEEDING**

This project involves the use of Lithium-Ion (Li-Ion) or Lithium Polymer (LiPo) batteries. If handled improperly, these batteries can overheat, catch fire, or explode.
* **I assume no liability** for any damage to property, people, or components resulting from the construction or use of this project.
* Always double-check polarity before connecting the batteries.
* Do not attempt to assemble this circuit if you are not experienced in handling lithium batteries.

## 🌟 Key Features

* **Microcontroller:** [STM32F401RBT6]
* **Battery Support:** [12x 18650 cells in 4s3p configuration]
* **Input:** USB-C (PD supported)
* **Output:** [2 PD USB-C, 2 3A USB-A]
* **Interface:** [ 4 Status LEDs, I2C OLED Display, Pressable encoder]
* **Protections:** Overcharge, Overdischarge, Short-circuit, Thermal protection.

## 📂 Repository Structure

```text
Ducker-Charger/
├── hardware/          # KiCad Project (Schematics and PCB Layout)
├── firmware/          # Source Code 
├── mechanical/        # 3D Enclosure files 
├── docs/              # Documentation
└── img/               # Images for this README
