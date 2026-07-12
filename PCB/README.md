# PCB — KiCad Projects

Two KiCad projects make up the Ducker-Charger hardware:

| Project | Contents |
| --- | --- |
| `Ducker-Charger/` | Main charger board: schematics, PCB layout, production files |
| `Sensing-Board/` | Cell sensing board (tap connections from the 4S pack to the BMS) |

## Main board schematic structure

| Sheet | File | Contents |
| --- | --- | --- |
| Root | `Ducker-Charger.kicad_sch` | Top level, sheet interconnections |
| High power | `high_pow.kicad_sch` | TPS25750 (primary USB-C PD), BQ25713 buck-boost charger, VBUS power path |
| Low power | `low_pow.kicad_sch` | CYPD3175 (secondary USB-C PD), STPD01 programmable rail, USB-A load switches, INA3221 sensing, lab-output switching |
| Logic | `Logic.kicad_sch` | STM32F401RBT6, display/encoder interface, logic supply |
| BMS | `BMS.kicad_sch` | BQ77915 protection, BQ34Z100-R2 fuel gauge, ISO1540 I2C isolator, protection FETs |

> The secondary PD controller is the Infineon **CYPD3175** — it replaced the
> ST **STUSB4710** (EOL).

Component datasheets and technical reference manuals used during the design are
collected in [`Ducker-Charger/docs/`](Ducker-Charger/docs/).

## System block diagram

```mermaid
flowchart TB
    subgraph BMS["BMS (always-on hardware safety)"]
        PACK["4S3P Sony VTC5<br/>14.4V nom / 16.8V max"]
        BQ77915["BQ77915 protector<br/>+ N-ch FETs (low-side)"]
        GAUGE["BQ34Z100-R2 fuel gauge<br/>(battery-referenced)"]
        PACK --> BQ77915
        PACK -.->|sense| GAUGE
    end

    subgraph HP["High-power stage"]
        TPS["TPS25750<br/>primary USB-C PD"]
        BQ25713["BQ25713<br/>buck-boost NVDC charger"]
        USBC1(("USB-C1<br/>bidirectional PD"))
        USBC1 <--> TPS
        TPS <-->|private I2C_EX| BQ25713
    end

    subgraph LP["Low-power / aux stage"]
        CYPD["CYPD3175<br/>secondary USB-C PD"]
        STPD01["STPD01<br/>programmable rail"]
        USBC2(("USB-C2<br/>source-only"))
        USBA(("2x USB-A<br/>5V"))
        LAB(("Lab output"))
        INA["INA3221<br/>current monitor"]
        CYPD <--> USBC2
        STPD01 --> USBC2
        STPD01 --> LAB
        INA --- USBA
    end

    subgraph LOGIC["Logic"]
        MCU["STM32F401RBT6"]
        DISP["ILI9341 TFT (SPI1)"]
        ENC["Rotary encoder (TIM3)"]
        MCU --> DISP
        ENC --> MCU
    end

    BQ77915 -->|+BATT| BQ25713
    BQ25713 -->|VSYS| LP
    BQ25713 -->|VSYS| LOGIC

    MCU <-->|"I2C3 (PD bus)"| TPS
    MCU <-->|"I2C1 (system bus, HPI)"| CYPD
    MCU <-->|I2C1| STPD01
    MCU <-->|I2C1| INA
    MCU <-->|"I2C1 via ISO1540"| GAUGE
```

Bus-level detail (pins, addresses):
[`docs/hardware/BSP_reference.md`](../docs/hardware/BSP_reference.md).

## Production

`*/production/` holds the fabrication outputs (gerber zip + IPC netlist)
generated with the JLCPCB fabrication toolkit. Regenerate after any layout
change — do not hand-edit.
