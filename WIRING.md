# Detailed Wiring Guide

## Visual Wiring Diagram

```
                           ESP32 DevKit
                    ┌─────────────────────┐
                    │                     │
    INMP441         │                     │         MAX98357
  ┌─────────┐       │                     │       ┌──────────┐
  │         │       │                     │       │          │
  │ VDD ────┼───────┤ 3.3V                │       │          │
  │ GND ────┼───────┤ GND ────────────────┼───────┤ GND      │
  │ SD  ────┼───────┤ GPIO 33             │       │          │
  │ WS  ────┼───────┤ GPIO 25             │       │          │
  │ SCK ────┼───────┤ GPIO 26             │       │          │
  │ L/R ────┼───────┤ GND                 │       │          │
  │         │       │                     │       │          │
  └─────────┘       │                     │       │ VIN  ────┼──── 5V
                    │                     │       │ DIN  ────┼──── GPIO 12
                    │                     │       │ BCLK ────┼──── GPIO 27
                    │                     │       │ LRC  ────┼──── GPIO 14
                    │                     │       │          │
                    │                     │       └──────────┘
                    │                     │              │
                    │                     │              │
                    │                     │         ┌────┴────┐
                    │                     │         │ Speaker │
                    │                     │         │  4-8Ω   │
                    │                     │         └─────────┘
                    └─────────────────────┘
```

## Component Details

### INMP441 I2S MEMS Microphone

**Pinout:**
```
     ┌─────────┐
     │  INMP441│
     ├─────────┤
  1  │ VDD     │ ← 3.3V Power
  2  │ GND     │ ← Ground
  3  │ SD      │ ← Serial Data Out
  4  │ WS      │ ← Word Select (Left/Right)
  5  │ SCK     │ ← Serial Clock
  6  │ L/R     │ ← Channel Select (GND=Left, VDD=Right)
     └─────────┘
```

**Important Notes:**
- **L/R Pin**: Must be connected to GND for left channel
- **Power**: Use 3.3V (not 5V)
- **Orientation**: Check PCB markings for microphone hole direction

### MAX98357 I2S Audio Amplifier

**Pinout:**
```
     ┌──────────┐
     │ MAX98357 │
     ├──────────┤
  1  │ VIN      │ ← 5V Power (or 3.3V)
  2  │ GND      │ ← Ground
  3  │ DIN      │ ← Data Input
  4  │ BCLK     │ ← Bit Clock
  5  │ LRC      │ ← Left/Right Clock
  6  │ GAIN     │ ← Gain Control (optional)
  7  │ SD       │ ← Shutdown (optional)
  8  │ +        │ → Speaker Positive
  9  │ -        │ → Speaker Negative
     └──────────┘
```

**Important Notes:**
- **Power**: 5V recommended for better volume, 3.3V also works
- **GAIN Pin**: 
  - Not connected = 15dB (default)
  - GND = 9dB
  - VIN = 12dB
- **SD Pin**: Leave floating or connect to GND for normal operation
- **Speaker**: Use 4Ω or 8Ω speaker, 3W or higher

## Step-by-Step Wiring Instructions

### Step 1: Prepare Components

1. Gather all components
2. Check ESP32 pin labels
3. Identify INMP441 and MAX98357 pins

### Step 2: Wire INMP441 Microphone

1. **Power Connections:**
   ```
   INMP441 VDD → ESP32 3.3V
   INMP441 GND → ESP32 GND
   ```

2. **I2S Data Connections:**
   ```
   INMP441 SD  → ESP32 GPIO 33
   INMP441 WS  → ESP32 GPIO 25
   INMP441 SCK → ESP32 GPIO 26
   ```

3. **Channel Select:**
   ```
   INMP441 L/R → ESP32 GND (for left channel)
   ```

### Step 3: Wire MAX98357 Amplifier

1. **Power Connections:**
   ```
   MAX98357 VIN → ESP32 5V (or 3.3V)
   MAX98357 GND → ESP32 GND
   ```

2. **I2S Data Connections:**
   ```
   MAX98357 DIN  → ESP32 GPIO 12
   MAX98357 BCLK → ESP32 GPIO 27
   MAX98357 LRC  → ESP32 GPIO 14
   ```

3. **Optional Pins:**
   ```
   MAX98357 GAIN → Leave floating (15dB default)
   MAX98357 SD   → Leave floating or GND
   ```

### Step 4: Connect Speaker

1. Connect speaker wires to MAX98357 speaker terminals
2. Observe polarity (+ and -)
3. Ensure secure connections

### Step 5: Verify Connections

**Checklist:**
- [ ] All power connections secure
- [ ] All ground connections common
- [ ] No short circuits
- [ ] INMP441 L/R connected to GND
- [ ] Speaker properly connected
- [ ] All I2S pins correctly wired

## Common Wiring Mistakes

### ❌ Wrong: INMP441 L/R not connected
```
INMP441 L/R → (floating)  ❌ Will cause channel issues
```
**✅ Correct:**
```
INMP441 L/R → GND  ✅ Left channel selected
```

### ❌ Wrong: Using 5V for INMP441
```
INMP441 VDD → 5V  ❌ May damage microphone
```
**✅ Correct:**
```
INMP441 VDD → 3.3V  ✅ Correct voltage
```

### ❌ Wrong: MAX98357 SD pulled high
```
MAX98357 SD → VIN  ❌ Shutdown mode, no audio
```
**✅ Correct:**
```
MAX98357 SD → (floating) or GND  ✅ Normal operation
```

### ❌ Wrong: Separate grounds
```
INMP441 GND → ESP32 GND
MAX98357 GND → (different ground)  ❌ Ground loop issues
```
**✅ Correct:**
```
All GND pins → Common ESP32 GND  ✅ Single ground reference
```

## Testing Connections

### Multimeter Tests

1. **Power Test:**
   - Measure 3.3V between INMP441 VDD and GND
   - Measure 5V (or 3.3V) between MAX98357 VIN and GND

2. **Continuity Test:**
   - Verify all signal connections
   - Check for shorts between adjacent pins

3. **Ground Test:**
   - Verify all grounds are connected
   - Check for ground loops

### Visual Inspection

1. Check for loose connections
2. Verify no exposed wire touching other pins
3. Ensure proper orientation of components
4. Check solder joints if using permanent connections

## Breadboard Layout Example

```
     ESP32 DevKit
    ┌─────────────┐
    │ 3.3V  5V    │
    │  │     │    │
    │  │     │    │
    └──┼─────┼────┘
       │     │
    ┌──┴─┐ ┌─┴──┐
    │INMP│ │MAX │
    │441 │ │9835│
    └────┘ └──┬─┘
              │
         ┌────┴────┐
         │ Speaker │
         └─────────┘
```

## Alternative Pin Configurations

If you need to use different pins, update these definitions in [`src/main.cpp`](src/main.cpp:15-24):

```cpp
// Microphone pins (can be changed)
#define I2S_MIC_WS            25  // Any GPIO
#define I2S_MIC_SCK           26  // Any GPIO
#define I2S_MIC_SD            33  // Any GPIO

// Speaker pins (can be changed)
#define I2S_SPK_BCLK          27  // Any GPIO
#define I2S_SPK_LRC           14  // Any GPIO
#define I2S_SPK_DOUT          12  // Any GPIO
```

**Note:** Avoid using GPIO 0, 2, 12, 15 as they affect boot mode.

## Power Considerations

### Current Requirements

- **ESP32**: ~240mA (WiFi active)
- **INMP441**: ~1.4mA
- **MAX98357**: Up to 3.2W @ 4Ω (depends on volume)

### Power Supply Options

1. **USB Power (5V):**
   - Sufficient for most applications
   - Use quality USB cable
   - May need external power for high volume

2. **External Power:**
   - Use 5V regulated supply
   - Minimum 2A recommended
   - Connect to VIN and GND

3. **Battery Power:**
   - Use 3.7V LiPo with boost converter
   - Or 5V power bank
   - Consider power consumption

## Troubleshooting Wiring Issues

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| No microphone input | Wrong pin connections | Verify GPIO numbers |
| | L/R not grounded | Connect L/R to GND |
| | No power | Check 3.3V supply |
| No speaker output | Wrong pin connections | Verify GPIO numbers |
| | SD pin high | Check SD pin state |
| | No power | Check 5V supply |
| Distorted audio | Loose connections | Secure all wires |
| | Ground loop | Use common ground |
| | Insufficient power | Use better power supply |
| ESP32 won't boot | GPIO 0/2/12/15 issue | Avoid these pins or disconnect during boot |

## Safety Notes

⚠️ **Important Safety Information:**

1. **Never exceed voltage ratings:**
   - INMP441: 3.3V maximum
   - MAX98357: 5.5V maximum
   - ESP32: 3.3V on GPIO pins

2. **Check polarity:**
   - Wrong polarity can damage components
   - Double-check before powering on

3. **Avoid short circuits:**
   - Keep wires organized
   - Use heat shrink or tape on exposed connections

4. **Speaker protection:**
   - Don't exceed speaker power rating
   - Use appropriate impedance (4-8Ω)

5. **Static protection:**
   - Handle components by edges
   - Use anti-static precautions

---

**Ready to wire? Follow the steps carefully and double-check each connection!** 🔌