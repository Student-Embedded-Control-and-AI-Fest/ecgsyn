
<p align="center">
    <img width="800"alt="demo" src="https://github.com/Student-Embedded-Control-and-AI-Fest/ecgsyn/blob/main/docs/tdm_zoh_device.jpeg" />
</p>

# TDM/ZOH Analog Generator

This document summarizes the current hardware wiring for the **dual-DAC time-division-multiplexed analog output generator** and the independent **STM32F411 Black Pill ADC validator**.

The present hardware test uses **all 16 analog outputs** available from two CD4051BE multiplexers. The same hardware will later be reused for ECGSYN and other multichannel analog-output experiments.

- **Generator MCU:** WeActStudio STM32H523 Core Board
- **Validator MCU:** WeActStudio STM32F411CEU6 Black Pill
- **Analog demultiplexers:** 2 × CD4051BE
- **Hold capacitors:** start with **47 nF (`473`)** per analog output
- **Display:** 128×64 SSD1306 I²C OLED
- **Generator architecture:** 2 DACs → 2 × CD4051 → 16 held analog outputs
- **Logical output sample/frame rate:** 250 Hz
- **TDM phases per frame:** 8
- **MUX service rate:** 2 kHz
- **Phase period:** 500 µs
- **Frame sync:** STM32H523 PB8 → STM32F411 PB10

---

# 1. CD4051BE DIP-16 Pinout

Top view, notch at the top:

```text
            ┌───U───┐
 Y4   pin 1 │       │ pin 16  VDD
 Y6   pin 2 │       │ pin 15  Y2
 COM  pin 3 │       │ pin 14  Y1
 Y7   pin 4 │       │ pin 13  Y0
 Y5   pin 5 │       │ pin 12  Y3
 INH  pin 6 │       │ pin 11  A
 VEE  pin 7 │       │ pin 10  B
 VSS  pin 8 │_______│ pin  9  C
```

## 1.1 Pin functions

| Pin | Name | Function |
|---:|---|---|
| 1 | Y4 | Analog channel 4 |
| 2 | Y6 | Analog channel 6 |
| 3 | COM | Common analog terminal; connect to DAC |
| 4 | Y7 | Analog channel 7 |
| 5 | Y5 | Analog channel 5 |
| 6 | INH | Inhibit; HIGH disconnects all channels |
| 7 | VEE | Negative analog supply; tie to GND for unipolar operation |
| 8 | VSS | Ground |
| 9 | C | Address bit C |
| 10 | B | Address bit B |
| 11 | A | Address bit A |
| 12 | Y3 | Analog channel 3 |
| 13 | Y0 | Analog channel 0 |
| 14 | Y1 | Analog channel 1 |
| 15 | Y2 | Analog channel 2 |
| 16 | VDD | Positive supply |

For the current 0–3.3 V prototype:

```text
VDD pin 16 -> 3.3 V
VSS pin  8 -> GND
VEE pin  7 -> GND
```

Add **100 nF decoupling** from VDD to GND close to each CD4051.

---

# 2. Generator: WeActStudio STM32H523

The STM32H523 is the waveform generator.

Both internal DAC channels are used as active multiplexed sources. Neither DAC is permanently dedicated to a reference output.

## 2.1 DAC connections

```text
STM32H523 PA4 (DAC) -> pin 3 COM of CD4051 #1
STM32H523 PA5 (DAC) -> pin 3 COM of CD4051 #2
```

The two DACs may output different voltages simultaneously while both CD4051s select the same channel number.

---

# 3. Shared CD4051 Address Lines

Both CD4051s share the same three address GPIOs:

```text
STM32H523 PB3 -> A, pin 11, both CD4051s
STM32H523 PB4 -> B, pin 10, both CD4051s
STM32H523 PB5 -> C, pin  9, both CD4051s
```

Address sequence:

| C | B | A | Selected channel |
|---:|---:|---:|---|
| 0 | 0 | 0 | Y0 |
| 0 | 0 | 1 | Y1 |
| 0 | 1 | 0 | Y2 |
| 0 | 1 | 1 | Y3 |
| 1 | 0 | 0 | Y4 |
| 1 | 0 | 1 | Y5 |
| 1 | 1 | 0 | Y6 |
| 1 | 1 | 1 | Y7 |

All eight channels are now used on both muxes.

---

# 4. Shared INH Control

Use one STM32H523 GPIO for both CD4051 INH pins:

```text
STM32H523 PA8
      |
      +----> pin 6 INH, CD4051 #1
      |
      +----> pin 6 INH, CD4051 #2
```

Logic:

```text
PA8 LOW  -> INH LOW  -> selected channels connected / TRACK
PA8 HIGH -> INH HIGH -> all channels disconnected / HOLD
```

Recommended startup protection:

```text
PA8 / INH ---- 10 kΩ ---- 3.3 V
```

This keeps both muxes inhibited while the MCU is resetting or before PA8 is configured.

---

# 5. OLED Display

The existing small OLED is a **128×64 SSD1306 I²C display**.

Use I²C1:

```text
STM32H523 PB6 -> OLED SCL
STM32H523 PB7 -> OLED SDA
STM32H523 3V3 -> OLED VCC
STM32H523 GND -> OLED GND
```

Typical SSD1306 I²C address:

```text
0x3C
```

The OLED can be used instead of USB CDC for local generator status.

Suggested display during the current dummy test:

```text
TDM 16CH
Fs: 250 Hz
MUX: 2.0 kHz
RUN
```

Later for ECGSYN:

```text
ECGSYN
Fs: 250 Hz
MUX: 2.0 kHz
RUN
```

**PB6 and PB7 are therefore reserved for I²C1 and must not be reused as frame-sync GPIOs.**

---

# 6. Frame-Sync Connection

Use a dedicated digital synchronization wire between generator and validator:

```text
STM32H523 PB8  ->  STM32F411 PB10
```

Direction:

```text
H523 PB8  = FRAME_SYNC output
F411 PB10 = FRAME_SYNC input
```

The H523 should generate one sync pulse at the boundary of each completed 8-phase output frame.

At 250 frames/s:

```text
FRAME_SYNC frequency = 250 Hz
```

This allows the F411 validator to align ADC captures to the H523 output-frame timing and removes the arbitrary acquisition phase seen when starting serial captures asynchronously.

---

# 7. Generator Pin Summary

| STM32H523 pin | Function | Connection |
|---|---|---|
| PA4 | DAC #1 | COM pin 3, CD4051 #1 |
| PA5 | DAC #2 | COM pin 3, CD4051 #2 |
| PB3 | MUX address A | pin 11 on both muxes |
| PB4 | MUX address B | pin 10 on both muxes |
| PB5 | MUX address C | pin 9 on both muxes |
| PA8 | Shared INH | pin 6 on both muxes |
| PB6 | I²C1 SCL | SSD1306 SCL |
| PB7 | I²C1 SDA | SSD1306 SDA |
| PB8 | FRAME_SYNC | F411 PB10 |
| 3.3 V | Supply | mux VDD + OLED VCC |
| GND | Common ground | mux VSS/VEE + OLED + validator |

---

# 8. ZOH / Sample-and-Hold Outputs

Each CD4051 output receives its own hold capacitor.

Initial capacitor value:

```text
47 nF = marking 473
```

Example:

```text
CD4051 Y0 -----+------> optional buffer ------> analog output
               |
              47 nF
               |
              GND
```

Repeat this for **all Y0–Y7 outputs** on both CD4051s.

## 8.1 Capacitor pin list

```text
Y0 pin 13 -> 47 nF -> GND
Y1 pin 14 -> 47 nF -> GND
Y2 pin 15 -> 47 nF -> GND
Y3 pin 12 -> 47 nF -> GND
Y4 pin  1 -> 47 nF -> GND
Y5 pin  5 -> 47 nF -> GND
Y6 pin  2 -> 47 nF -> GND
Y7 pin  4 -> 47 nF -> GND
```

The hold capacitor belongs on the **generator/ZOH side**.

Do **not** add another large hold capacitor to the validator ADC inputs, because that would alter the waveform being measured.

A high-input-impedance voltage follower after each hold capacitor is recommended for the final system:

```text
CD4051 Yx
    |
    +---- 47 nF ---- GND
    |
    +----> op-amp voltage follower ----> OUTx
```

---

# 9. Full 16-Output Mapping

Both muxes advance together.

```text
Phase 0: PA4 -> MUX1 Y0 -> OUT0      PA5 -> MUX2 Y0 -> OUT8
Phase 1: PA4 -> MUX1 Y1 -> OUT1      PA5 -> MUX2 Y1 -> OUT9
Phase 2: PA4 -> MUX1 Y2 -> OUT2      PA5 -> MUX2 Y2 -> OUT10
Phase 3: PA4 -> MUX1 Y3 -> OUT3      PA5 -> MUX2 Y3 -> OUT11
Phase 4: PA4 -> MUX1 Y4 -> OUT4      PA5 -> MUX2 Y4 -> OUT12
Phase 5: PA4 -> MUX1 Y5 -> OUT5      PA5 -> MUX2 Y5 -> OUT13
Phase 6: PA4 -> MUX1 Y6 -> OUT6      PA5 -> MUX2 Y6 -> OUT14
Phase 7: PA4 -> MUX1 Y7 -> OUT7      PA5 -> MUX2 Y7 -> OUT15
```

The application may assign these 16 physical outputs however it wishes.

Examples:

```text
15 generated signals + 1 reference
11 ECG signals       + 1 reference + spare outputs
16 dummy/test signals
```

The reference is simply another logical output channel; an entire DAC does not need to be dedicated to it.

---

# 10. Current Dummy Sine Test

The first hardware validation uses 16 same-frequency, same-phase sinusoidal signals with different peak amplitudes.

Logical output amplitudes:

```text
OUT0   = 1 V peak
OUT1   = 2 V peak
OUT2   = 3 V peak
OUT3   = 1 V peak
OUT4   = 2 V peak
OUT5   = 3 V peak
OUT6   = 1 V peak
OUT7   = 2 V peak

OUT8   = 3 V peak
OUT9   = 1 V peak
OUT10  = 2 V peak
OUT11  = 3 V peak
OUT12  = 1 V peak
OUT13  = 2 V peak
OUT14  = 3 V peak
OUT15  = 1 V peak
```

The present dummy waveform is a slow unipolar sine suitable for the 0–3.3 V analog range.

For amplitude \(A_i\):

```text
V_i(t) = A_i/2 × [1 + sin(2π f t)]
```

This gives approximately:

```text
1 V channel -> 0...1 V
2 V channel -> 0...2 V
3 V channel -> 0...3 V
```

---

# 11. TDM Timing

For a 250 Hz logical output-frame rate:

```text
Frame period = 1 / 250 Hz = 4 ms
```

Eight phases are required because each DAC services eight held outputs:

```text
MUX phase rate = 250 Hz × 8 = 2000 Hz
Phase period    = 1 / 2000  = 500 µs
```

Conceptual sequence:

```text
1. INH = HIGH        disconnect both muxes; previous outputs HOLD
2. Set PB3/PB4/PB5   choose Y0...Y7
3. Update PA4 DAC and PA5 DAC
4. INH = LOW         selected pair TRACKS / charges
5. Remain connected for most of the 500 µs slot
6. Next timer interrupt:
       INH = HIGH
       current pair enters HOLD
       advance to next phase
```

After phase 7:

```text
advance logical waveform sample
generate FRAME_SYNC pulse on PB8
return to phase 0
```

Thus:

```text
TIM6 ISR             = 2 kHz
logical frame/sample = 250 Hz
FRAME_SYNC           = 250 Hz
```

---

# 12. Validator: STM32F411CEU6 Black Pill

The STM32F411 is used as an independent ADC measurement instrument.

The current board has its optional SPI-flash footprint **unpopulated**, so PA4–PA7 are available.

Direct ADC pins:

| Black Pill pin | ADC input |
|---|---|
| PA0 | ADC1_IN0 |
| PA1 | ADC1_IN1 |
| PA2 | ADC1_IN2 |
| PA3 | ADC1_IN3 |
| PA4 | ADC1_IN4 |
| PA5 | ADC1_IN5 |
| PA6 | ADC1_IN6 |
| PA7 | ADC1_IN7 |
| PB0 | ADC1_IN8 |
| PB1 | ADC1_IN9 |

This gives **10 directly accessible external ADC inputs**.

The currently soldered validator header contains:

```text
PA0
PA1
PA2
PA3
PA4
PA5
PA6
PA7
PB0
PB1
GND
3V3   optional
```

Additional sync input:

```text
PB10 <- FRAME_SYNC from H523 PB8
```

## 12.1 Current Arduino validator protocol

The F411 validator presently behaves as a selectable single-channel ADC instrument:

```text
Serial '0' -> PA0
Serial '1' -> PA1
Serial '2' -> PA2
Serial '3' -> PA3
Serial '4' -> PA4
Serial '5' -> PA5
Serial '6' -> PA6
Serial '7' -> PA7
Serial '8' -> PB0
Serial '9' -> PB1
```

After selection it streams one ADC sample per serial line.

The current sampling rate is approximately:

```text
1 kHz
```

This gives about 200 ADC samples per cycle for a 5 Hz dummy sine.

### PA0 note

PA0 is also connected to the Black Pill user button. It can still be used as an ADC input, but do not press the button while acquiring data.

---

# 13. Connecting Generator to Validator

For initial testing, connect held/buffered outputs directly using Dupont wires.

Example:

```text
H523 generator OUT0 -> F411 PA0
H523 generator OUT1 -> F411 PA1
H523 generator OUT2 -> F411 PA2
...
```

Frame synchronization:

```text
H523 PB8 -> F411 PB10
```

Most importantly:

```text
H523 GND -------- F411 GND
```

The generator and validator must share a common reference/ground.

The validator ADC inputs should initially have **no additional large capacitor to GND**.

This is intentional: the validator should observe the real ZOH behavior, including droop, settling error, and switching artifacts.

---

# 14. Overall Architecture

```text
                         GENERATOR
                   WeAct STM32H523

              PA4 DAC             PA5 DAC
                 |                   |
            CD4051 #1          CD4051 #2
                 |                   |
             8 × ZOH              8 × ZOH
                 |                   |
            OUT0..OUT7         OUT8..OUT15
                 \                   /
                  \                 /
                   +---------------+
                           |
                    analog outputs
                           |
                    Dupont/header
                           |
                           v
                        VALIDATOR
                STM32F411CEU6 Black Pill
                           |
                     selected ADC
                           |
                           v
                       USB serial
                           |
                           v
                      Python plotter

Control/status:

H523 PB3/PB4/PB5 ---> both CD4051 A/B/C
H523 PA8 ---------> both CD4051 INH
H523 PB6/PB7 -----> SSD1306 OLED
H523 PB8 ---------> F411 PB10 FRAME_SYNC
```

The generator is therefore a **dual-DAC TDM + analog demultiplexing + per-channel ZOH system**, while the F411 acts as an independent acquisition/validation device.

---

# 15. Current Wiring Checklist

## Generator

- [ ] H523 PA4 -> CD4051 #1 COM pin 3
- [ ] H523 PA5 -> CD4051 #2 COM pin 3
- [ ] H523 PB3 -> A pin 11 on both muxes
- [ ] H523 PB4 -> B pin 10 on both muxes
- [ ] H523 PB5 -> C pin 9 on both muxes
- [ ] H523 PA8 -> INH pin 6 on both muxes
- [ ] 10 kΩ pull-up from shared INH to 3.3 V
- [ ] Both CD4051 pin 16 VDD -> 3.3 V
- [ ] Both CD4051 pin 8 VSS -> GND
- [ ] Both CD4051 pin 7 VEE -> GND
- [ ] 100 nF decoupling capacitor at each CD4051
- [ ] 47 nF hold capacitor from each Y0–Y7 output to GND
- [ ] H523 PB6 -> OLED SCL
- [ ] H523 PB7 -> OLED SDA
- [ ] OLED VCC -> 3.3 V
- [ ] OLED GND -> GND
- [ ] H523 PB8 -> F411 PB10 FRAME_SYNC
- [ ] Common system ground

## Validator

- [ ] PA0–PA7, PB0, PB1 available on ADC header
- [ ] GND available on header
- [ ] Optional 3.3 V available on header
- [ ] PB10 wired as FRAME_SYNC input
- [ ] No extra large capacitors on ADC inputs
- [ ] F411 GND connected to H523 GND

---

# 16. Locked Pin Assignment

```text
STM32H523 GENERATOR
-------------------
PA4   DAC #1
PA5   DAC #2

PB3   CD4051 A
PB4   CD4051 B
PB5   CD4051 C
PA8   CD4051 shared INH

PB6   SSD1306 SCL
PB7   SSD1306 SDA

PB8   FRAME_SYNC output


STM32F411 VALIDATOR
-------------------
PA0   ADC0
PA1   ADC1
PA2   ADC2
PA3   ADC3
PA4   ADC4
PA5   ADC5
PA6   ADC6
PA7   ADC7
PB0   ADC8
PB1   ADC9

PB10  FRAME_SYNC input
```
