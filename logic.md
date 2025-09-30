# 3S3P 18650 Active Balancing Board — Design Notes

## 🧠 MCU and Control Logic

The system uses an **ARM Cortex-M4** microcontroller (preferably from ST’s STM32 family) to handle:
- Active balancing between the three series groups  
- Measurement of voltage and temperature  
- Communication with and control of a small display

### Recommended STM32 MCU Models

| MCU | Max Freq | ADC | Flash / RAM | Notes |
|-----|----------|-----|-------------|-------|
| **STM32F446xx** | 180 MHz | 3 × 12 bit | Up to 512 KB / good | Strong balance of performance and features. |
| **STM32F479AG** | 180 MHz | 3 × 12 bit | Large | High-end option with extensive peripherals. |
| **STM32F427ZIT6** | 168 MHz | 3 × 12 bit | Large | Many I/O pins, suitable for complex systems with display and logging. |
| **STM32F411RET6** | 100 MHz | 16 channels | Smaller | Lower cost, appropriate for simpler designs. |

### MCU Selection Criteria

- **ADC**: Enough channels for cell voltages and temperature sensing  
- **Timers / PWM**: For MOSFET driving during balancing  
- **Interfaces**: I²C or SPI for the display, UART/CAN for debugging or external communication  
- **Memory**: ≥ 256 KB Flash and ≥ 64 KB RAM recommended  
- **Clock**: 80–180 MHz preferred

---

## 🔋 Battery Pack Topology

- **3S3P configuration** of 18650 cells  
- Active balancing between series groups using MOSFETs and either capacitive or DC-DC transfer methods  
- Voltage monitoring via:
  - ADC and resistor dividers, or
  - **LTC68xx** series IC for higher precision

---

## 🌡️ Sensors and Inputs

- Three NTC thermistors, one per parallel group  
- Optional additional thermistor for PCB temperature  
- Voltage sensing for each cell group (B1, B2, B3)  
- Shunt resistor and amplifier for current measurement

---

## ⚡ Power & USB

- Voltage regulator for MCU and peripherals  
- **USB-C ×2** and **USB-A ×1** ports  
- Power Delivery (PD) controller IC  
- Recharge / charging module for the pack

---

## 🧱 Main Components So Far

- 3 × NTC thermistors  
- Voltage regulator  
- PD IC  
- USB-C ×2, USB-A ×1  
- MCU (Cortex-M4)  
- MOSFETs for balancing  
- LTC68xx IC for voltage measurement  
- Recharge / charging module  
- Resistors and capacitors

---

## 📝 Additional Components to Consider

### Power & Protection
- Fuses or PTCs  
- TVS diodes on USB and battery terminals  
- Reverse polarity protection (diodes or back-to-back MOSFETs)  
- Pre-charge resistors to limit inrush current

### Sensing
- Shunt resistor + current sense amplifier  
- RC filters for ADC inputs

### Monitoring & Safety
- Brown-out / power supervisor  
- Hardware watchdog  
- Bootloader / safe-mode switch  
- Extra NTC for PCB temperature

### Connectors & Interfaces
- Battery pack connectors  
- Test points for voltage sense lines  
- SWD/JTAG connector  
- Pull-ups and resistors for USB-C CC lines

### Gate Drive & Balancing
- Gate resistors  
- Gate driver IC if required  
- Zener clamps or RC snubbers  
- Balancing capacitors or inductors depending on topology

### ADC & Precision
- Precision resistors for dividers (0.1%, low tempco)  
- Filtering capacitors  
- Optional external precision ADC

### MCU Support
- Crystal oscillator if needed  
- Decoupling capacitors and ferrite beads  
- RTC battery if applicable

### EMI & Filtering
- Ferrite beads on power lines  
- Common-mode chokes for USB  
- Proper analog/digital ground separation

### Mechanical & Debug
- Mounting holes, status LEDs  
- Test points for critical signals  
- Reset and bootloader buttons

### Communication & UI
- Level shifters if required  
- USB-to-UART bridge or header for debugging  
- Display connectors with pull-ups

### Memory / Logging
- Optional EEPROM or FRAM for persistent storage

---

## 🧭 Design Considerations

- **USB-C PD**: Proper CC resistor configuration, current/voltage compliance, and thermal design  
- **LTC680x**: Excellent for cell measurement; active balancing typically requires separate circuitry  
- **Layout**:
  - Separate power, analog, and digital planes  
  - Keep high-current paths away from ADC traces  
  - Use wide copper pours for power lines

- **Testing**: Add test points for all key nodes (cell voltages, shunt, NTC, reference, USB lines)  
- **Regulations**: If commercializing, ensure EMC and safety compliance

---

## 🛠️ Next Design Steps

1. Draw a **block diagram** showing power, sensing, balancing, communication, and control subsystems  
2. Finalize MCU selection (e.g., STM32F446RE is a strong candidate)  
3. Build a **complete BOM**, including protection and auxiliary components  
4. Draft the schematic and plan PCB layout with clear power / analog / digital separation

---

