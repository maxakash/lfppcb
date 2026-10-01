"""Part library for the LFP-8 board.

Every part used on the PCB is defined exactly once here, with
  * the KiCad 7 symbol (lib_id) and footprint,
  * the pin-number -> pin-name map (pin numbers = footprint pad numbers),
  * the JLCPCB/LCSC part number and library class (Basic/Preferred/Extended),
  * an optional SPICE template used by the simulation netlister.

LCSC numbers were checked offline (2026-09/10) against
  - the JLCPCB basic/preferred list (CDFER/jlcpcb-parts-database, 2026-10-01)
  - the yaqwsx/jlcparts component cache (stock/price snapshot 2026-09-30).
`verified` records which source confirmed the number.
"""
from dataclasses import dataclass, field
from typing import Dict, Optional, List


@dataclass
class PartDef:
    key: str
    symbol: str                 # KiCad lib_id
    footprint: str              # KiCad footprint lib:name
    pins: Dict[str, str]        # pad number -> pin name (names used by SPICE templates)
    lcsc: Optional[str] = None
    mfr: str = ""
    jlc: str = "Basic"          # Basic | Preferred | Extended | Hand (not assembled by JLCPCB)
    desc: str = ""
    spice: Optional[str] = None  # template; {ref} and {<pin name>} placeholders
    units: Optional[List[List[str]]] = None  # multi-unit symbols: pad numbers per unit
    verified: str = ""
    value: str = ""
    datasheet: str = ""


P: Dict[str, PartDef] = {}


def _add(p: PartDef):
    P[p.key] = p
    return p


TWO = {"1": "1", "2": "2"}

# ---------------------------------------------------------------- passives
# value-specific resistors (0603 1 % basic unless noted)
R0603 = {  # verified: JLCPCB basic/preferred list 2026-10-01 (all 1 %, 100 mW)
    "100": "C22775", "470": "C23179", "1k": "C21190", "1.2k": "C22765", "2k": "C22975",
    "3.3k": "C22978", "6.8k": "C23212", "30k": "C22984", "82k": "C23254", "1.5k": "C22843", "2.2k": "C4190",
    "2.4k": "C22940", "2.7k": "C13167", "4.3k": "C23159", "4.7k": "C23162", "5.1k": "C23186",
    "8.2k": "C25981", "9.1k": "C23260", "10k": "C25804", "12k": "C22790", "15k": "C22809",
    "18k": "C25810", "20k": "C4184", "22k": "C31850", "33k": "C4216", "39k": "C23153",
    "40.2k": "C12447", "43k": "C23172", "47k": "C25819", "49.9k": "C23184", "56k": "C23206",
    "68k": "C23231", "100k": "C25803", "1M": "C22935",
    "13k": "C22797", "27k": "C22967", "36k": "C23147", "51k": "C23196", "75k": "C23242",
    "91k": "C23265", "110k": "C25805", "150k": "C22807", "180k": "C22827", "200k": "C25811",
    "330k": "C23137", "360k": "C23146", "62k": "C23221", "240k": "C4197",
}
C0603 = {"1n": "C1588", "2.2n": "C1604", "10n": "C57112", "22n": "C21122", "47n": "C1622",
         "100n": "C14663", "220n": "C21120", "470n": "C1623", "1u": "C15849", "4.7u": "C19666",
         "10u": "C96446"}
C0805 = {"10u": "C15850", "22u": "C45783", "1u": "C28323", "47u": "C16780"}
C1206 = {"100u": "C15008", "22u": "C12891", "10u": "C13585"}


# On the JLCPCB basic/preferred list but library type "expand" (= preferred extended:
# no loading fee in Economic PCBA).  Source: /tmp/assembly-details.csv + ComponentList.csv,
# 2026-10-01 (CDFER jlcpcb-parts-database scrape).
R0603_PREFERRED = {"C23146", "C23147", "C23221", "C4197", "C23265", "C12447", "C23159"}


def resistor(value, size="0603"):
    key = f"R_{size}_{value}"
    if key in P:
        return P[key]
    lcsc = {"0603": R0603}[size][value]
    pref = lcsc in R0603_PREFERRED
    return _add(PartDef(key, "Device:R", f"Resistor_SMD:R_{size}_1608Metric", TWO,
                        lcsc=lcsc, jlc="Preferred" if pref else "Basic", value=value,
                        desc=f"Resistor {value} 1% {size}",
                        spice="R{ref} {1} {2} " + spice_val(value),
                        verified="cdfer-preferred (expand, no loading fee)" if pref else "cdfer-basic"))


def capacitor(value, size="0603"):
    key = f"C_{size}_{value}"
    if key in P:
        return P[key]
    table = {"0603": C0603, "0805": C0805, "1206": C1206}[size]
    metric = {"0603": "1608Metric", "0805": "2012Metric", "1206": "3216Metric"}[size]
    return _add(PartDef(key, "Device:C", f"Capacitor_SMD:C_{size}_{metric}", TWO,
                        lcsc=table[value], jlc="Basic", value=value,
                        desc=f"MLCC {value} {size}",
                        spice="C{ref} {1} {2} " + spice_val(value), verified="cdfer-basic"))


def spice_val(v: str) -> str:
    v = v.replace("µ", "u")
    if v.endswith("M") and not v.endswith("MEG"):
        return v[:-1] + "MEG"
    return v


# 2512 power resistors (Extended, verified in jlcparts cache 2026-09-30)
_add(PartDef("R_2512_11R", "Device:R", "Resistor_SMD:R_2512_6332Metric", TWO,
             lcsc="C7468192", mfr="FRP2512F11R0TS", jlc="Extended", value="11R 2W",
             desc="11 ohm 2 W 1 % 2512 (discharge load, 4 in parallel)",
             spice="R{ref} {1} {2} 11", verified="jlcparts 2026-09-30 stock 10644"))
_add(PartDef("R_2512_1R2", "Device:R", "Resistor_SMD:R_2512_6332Metric", TWO,
             lcsc="C7468247", mfr="FRP2512F1R20TS", jlc="Extended", value="1.2R 2W",
             desc="1.2 ohm 2 W 1 % 2512 (charge ballast + CC sense)",
             spice="R{ref} {1} {2} 1.2", verified="jlcparts 2026-09-30 stock 3231"))
_add(PartDef("R_SHUNT_0R1", "Device:R_Shunt", "LFP8:R_2512_Kelvin",
             {"1": "1", "2": "2", "3": "S1", "4": "S2"},
             lcsc="C7420048", mfr="FRM252WFR100TN", jlc="Extended", value="0.1R 1% 50ppm",
             desc="0.100 ohm 2 W 1 % 50 ppm/K current-sense 2512, Kelvin footprint",
             spice="R{ref} {1} {2} 0.1\nR{ref}k1 {1} {S1} 1m\nR{ref}k2 {2} {S2} 1m",
             verified="jlcparts 2026-09-30 stock 35627"))
_add(PartDef("PTC_2A", "Device:Polyfuse", "Fuse:Fuse_1812_4532Metric", TWO,
             lcsc="C18198342", mfr="1812L200/30GR", jlc="Extended", value="PTC 2A",
             desc="Resettable fuse 2.0 A hold / 4 A trip 30 V 1812",
             spice="X{ref} {1} {2} PTC", verified="jlcparts 2026-09-30 stock 79951"))

# ---------------------------------------------------------------- discretes
_add(PartDef("AO3400A", "Transistor_FET:AO3400A", "Package_TO_SOT_SMD:SOT-23",
             {"1": "G", "2": "S", "3": "D"}, lcsc="C20917", mfr="AO3400A", jlc="Basic",
             value="AO3400A", desc="N-MOSFET 30 V 5.7 A SOT-23",
             spice="M{ref} {D} {G} {S} AO3400A", verified="cdfer-basic + basic list 2026-10-01"))
_add(PartDef("AO3401A", "Transistor_FET:AO3401A", "Package_TO_SOT_SMD:SOT-23",
             {"1": "G", "2": "S", "3": "D"}, lcsc="C15127", mfr="AO3401A", jlc="Basic",
             value="AO3401A", desc="P-MOSFET 30 V 4 A SOT-23",
             spice="M{ref} {D} {G} {S} AO3401A", verified="cdfer-basic + basic list 2026-10-01"))
_add(PartDef("2N7002", "Transistor_FET:2N7002", "Package_TO_SOT_SMD:SOT-23",
             {"1": "G", "2": "S", "3": "D"}, lcsc="C8545", mfr="2N7002", jlc="Basic",
             value="2N7002", desc="N-MOSFET 60 V 115 mA SOT-23",
             spice="M{ref} {D} {G} {S} 2N7002", verified="cdfer-basic + basic list 2026-10-01"))
_add(PartDef("BSS138", "Transistor_FET:BSS138", "Package_TO_SOT_SMD:SOT-23",
             {"1": "G", "2": "S", "3": "D"}, lcsc="C7420339", mfr="BSS138", jlc="Preferred",
             value="BSS138", desc="N-MOSFET 50 V logic level shifter SOT-23",
             spice="M{ref} {D} {G} {S} BSS138", verified="cdfer-preferred"))
_add(PartDef("MMBT3904", "Transistor_BJT:MMBT3904", "Package_TO_SOT_SMD:SOT-23",
             {"1": "B", "2": "E", "3": "C"}, lcsc="C20526", mfr="MMBT3904", jlc="Basic",
             value="MMBT3904", desc="NPN 40 V 200 mA SOT-23",
             spice="Q{ref} {C} {B} {E} MMBT3904", verified="cdfer-basic"))
_add(PartDef("MMBT3906", "Transistor_BJT:MMBT3906", "Package_TO_SOT_SMD:SOT-23",
             {"1": "B", "2": "E", "3": "C"}, lcsc="C7420354", mfr="MMBT3906", jlc="Preferred",
             value="MMBT3906", desc="PNP 40 V 200 mA SOT-23",
             spice="Q{ref} {C} {B} {E} MMBT3906", verified="cdfer-preferred"))
_add(PartDef("SS34", "Device:D_Schottky", "Diode_SMD:D_SMA", {"1": "K", "2": "A"},
             lcsc="C8678", mfr="SS34", jlc="Basic", value="SS34",
             desc="Schottky 40 V 3 A SMA", spice="D{ref} {A} {K} SS34", verified="cdfer-basic"))
_add(PartDef("SS54", "Device:D_Schottky", "Diode_SMD:D_SMA", {"1": "K", "2": "A"},
             lcsc="C22452", mfr="SS54", jlc="Basic", value="SS54",
             desc="Schottky 40 V 5 A SMA", spice="D{ref} {A} {K} SS54", verified="cdfer-basic"))
_add(PartDef("SS14", "Device:D_Schottky", "Diode_SMD:D_SMA", {"1": "K", "2": "A"},
             lcsc="C2480", mfr="SS14", jlc="Basic", value="SS14",
             desc="Schottky 40 V 1 A SMA", spice="D{ref} {A} {K} SS14", verified="cdfer-basic"))
_add(PartDef("BAT54WS", "Device:D_Schottky", "Diode_SMD:D_SOD-323", {"1": "K", "2": "A"},
             lcsc="C7502694", mfr="BAT54WS", jlc="Preferred", value="BAT54WS",
             desc="Schottky 30 V 200 mA SOD-323", spice="D{ref} {A} {K} BAT54WS",
             verified="cdfer-preferred + jlcparts stock 411862"))
_add(PartDef("SMBJ6.0A", "Device:D_TVS", "Diode_SMD:D_SMB", {"1": "K", "2": "A"},
             lcsc="C19077560", mfr="SMBJ6.0A", jlc="Preferred", value="SMBJ6.0A",
             desc="TVS 6.0 V standoff 600 W SMB", spice="D{ref} {A} {K} SMBJ6V0A",
             verified="cdfer-preferred"))
_add(PartDef("LED_G", "Device:LED", "LED_SMD:LED_0805_2012Metric", {"1": "K", "2": "A"},
             lcsc="C2297", mfr="0805 green", jlc="Basic", value="Green",
             desc="LED green 0805", spice="D{ref} {A} {K} LED_G", verified="cdfer-basic"))
_add(PartDef("LED_R", "Device:LED", "LED_SMD:LED_0603_1608Metric", {"1": "K", "2": "A"},
             lcsc="C2286", mfr="0603 red", jlc="Basic", value="Red",
             desc="LED red 0603", spice="D{ref} {A} {K} LED_R", verified="cdfer-basic"))

# ---------------------------------------------------------------- ICs
LM324_UNITS = [["1", "2", "3"], ["5", "6", "7"], ["10", "9", "8"], ["12", "13", "14"], ["4", "11"]]
_add(PartDef("LM324", "Amplifier_Operational:LM324", "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
             {"1": "OUT1", "2": "IN1-", "3": "IN1+", "4": "V+", "5": "IN2+", "6": "IN2-", "7": "OUT2",
              "8": "OUT3", "9": "IN3-", "10": "IN3+", "11": "V-", "12": "IN4+", "13": "IN4-", "14": "OUT4"},
             lcsc="C71035", mfr="LM324DT", jlc="Basic", value="LM324",
             desc="Quad op-amp SOIC-14", units=LM324_UNITS,
             spice=("X{ref}A {IN1+} {IN1-} {V+} {V-} {OUT1} LM324 params: VOS={vos1}\n"
                    "X{ref}B {IN2+} {IN2-} {V+} {V-} {OUT2} LM324 params: VOS={vos2}\n"
                    "X{ref}C {IN3+} {IN3-} {V+} {V-} {OUT3} LM324 params: VOS={vos3}\n"
                    "X{ref}D {IN4+} {IN4-} {V+} {V-} {OUT4} LM324 params: VOS={vos4}"),
             verified="cdfer-basic"))
DUAL_UNITS = [["3", "2", "1"], ["5", "6", "7"], ["8", "4"]]
_add(PartDef("LM358", "Amplifier_Operational:LM358", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
             {"1": "OUT1", "2": "IN1-", "3": "IN1+", "4": "V-", "5": "IN2+", "6": "IN2-", "7": "OUT2", "8": "V+"},
             lcsc="C7950", mfr="LM358DR2G", jlc="Basic", value="LM358", desc="Dual op-amp SOIC-8",
             units=DUAL_UNITS,
             spice=("X{ref}A {IN1+} {IN1-} {V+} {V-} {OUT1} LM324 params: VOS={vos1}\n"
                    "X{ref}B {IN2+} {IN2-} {V+} {V-} {OUT2} LM324 params: VOS={vos2}"),
             verified="cdfer-basic"))
_add(PartDef("LM393", "Comparator:LM393", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
             {"1": "OUT1", "2": "IN1-", "3": "IN1+", "4": "V-", "5": "IN2+", "6": "IN2-", "7": "OUT2", "8": "V+"},
             lcsc="C7955", mfr="LM393DR2G", jlc="Basic", value="LM393", desc="Dual comparator SOIC-8",
             units=DUAL_UNITS,
             spice=("X{ref}A {IN1+} {IN1-} {V+} {V-} {OUT1} LM393 params: VOS={vos1}\n"
                    "X{ref}B {IN2+} {IN2-} {V+} {V-} {OUT2} LM393 params: VOS={vos2}"),
             verified="cdfer-basic"))
_add(PartDef("CJ431", "Reference_Voltage:TL431DBZ", "Package_TO_SOT_SMD:SOT-23",
             # CJ431 (C3113) datasheet: 1 REF, 2 K, 3 A.  KiCad TL431DBZ symbol: 1 K, 2 REF.
             # On this board REF and K are tied together, so either order is safe.
             {"1": "REF", "2": "K", "3": "A"},
             lcsc="C3113", mfr="CJ431", jlc="Basic", value="CJ431 2.495V",
             desc="Shunt reference 2.495 V 0.5 % SOT-23", spice="X{ref} {K} {A} {REF} TL431 params: VREF0={vref0}",
             verified="cdfer-basic"))
_add(PartDef("AMS1117-3.3", "Regulator_Linear:AMS1117-3.3", "Package_TO_SOT_SMD:SOT-223-3_TabPin2",
             {"1": "GND", "2": "VO", "3": "VI"}, lcsc="C6186", mfr="AMS1117-3.3", jlc="Basic",
             value="AMS1117-3.3", desc="LDO 3.3 V 1 A SOT-223",
             spice="B{ref} {VO} {GND} V = min(3.3, max(V({VI},{GND})-1.1,0))", verified="cdfer-basic"))
_add(PartDef("74HC595", "74xx:74HC595", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
             {"15": "QA", "1": "QB", "2": "QC", "3": "QD", "4": "QE", "5": "QF", "6": "QG", "7": "QH",
              "8": "GND", "9": "QH'", "10": "SRCLR", "11": "SRCLK", "12": "RCLK", "13": "OE", "14": "SER", "16": "VCC"},
             lcsc="C5947", mfr="74HC595D,118", jlc="Basic", value="74HC595", desc="8-bit shift register SOIC-16",
             verified="cdfer-basic"))
_add(PartDef("CD4051B", "Analog_Switch:CD4051B", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
             {"13": "X0", "14": "X1", "15": "X2", "12": "X3", "1": "X4", "5": "X5", "2": "X6", "4": "X7",
              "3": "X", "6": "INH", "7": "VEE", "8": "VSS", "9": "C", "10": "B", "11": "A", "16": "VDD"},
             lcsc="C21379", mfr="CD4051BM96", jlc="Preferred", value="CD4051B", desc="8:1 analog mux SOIC-16",
             verified="cdfer-preferred"))
_add(PartDef("ADS1115", "Analog_ADC:ADS1115IDGS", "Package_SO:TSSOP-10_3x3mm_P0.5mm",
             {"1": "ADDR", "2": "ALERT", "3": "GND", "4": "AIN0", "5": "AIN1", "6": "AIN2", "7": "AIN3",
              "8": "VDD", "9": "SDA", "10": "SCL"},
             lcsc="C37593", mfr="ADS1115IDGSR", jlc="Extended", value="ADS1115",
             desc="16-bit 4-ch I2C ADC VSSOP-10", verified="CDFER extended library (C37593)"))
_add(PartDef("ESP32-C3-WROOM-02", "LFP8:ESP32-C3-WROOM-02", "RF_Module:ESP32-C3-WROOM-02",
             {"1": "3V3", "2": "EN", "3": "IO4", "4": "IO5", "5": "IO6", "6": "IO7", "7": "IO8", "8": "IO9",
              "9": "GND", "10": "IO10", "11": "IO20", "12": "IO21", "13": "IO18", "14": "IO19", "15": "IO3",
              "16": "IO2", "17": "IO1", "18": "IO0", "19": "EPAD"},
             lcsc="C2934560", mfr="ESP32-C3-WROOM-02-N4", jlc="Extended", value="ESP32-C3-WROOM-02-N4",
             desc="Wi-Fi/BLE RISC-V module 4 MB flash, PCB antenna",
             verified="NOT verifiable offline - confirm during JLCPCB BOM matching"))

# ---------------------------------------------------------------- electromechanical (hand-soldered)
_add(PartDef("JST_XH_6", "Connector_Generic:Conn_01x06", "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical",
             {str(i): str(i) for i in range(1, 7)}, lcsc=None, mfr="JST B6B-XH-A (or clone)", jlc="Hand",
             value="XH-6P", desc="Cell connector: 1 B+F 2 B+S 3 B-S 4 B-F 5 NTC 6 GND"))
_add(PartDef("TERM_2", "Connector_Generic:Conn_01x02", "TerminalBlock:TerminalBlock_bornier-2_P5.08mm",
             TWO, lcsc=None, mfr="KF301-5.0-2P / 2-pin 5.08 mm 16 A terminal", jlc="Hand",
             value="PWR 5V", desc="Power input terminal block 5.08 mm"))
_add(PartDef("HDR_6", "Connector_Generic:Conn_01x06", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
             {str(i): str(i) for i in range(1, 7)}, lcsc=None, mfr="2.54 mm header", jlc="Hand",
             value="HDR-6", desc="2.54 mm pin header"))
_add(PartDef("HDR_2", "Connector_Generic:Conn_01x02", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
             TWO, lcsc=None, mfr="2.54 mm header", jlc="Hand", value="FAN", desc="Fan header"))
_add(PartDef("SW_TACT", "Switch:SW_Push", "LFP8:SW_SMD_5.1x5.1_4P", {"1": "1", "2": "2"},
             lcsc="C318884", mfr="TS-1088 type 5.1x5.1", jlc="Basic", value="SW",
             desc="Tactile switch SMD 5.1 x 5.1 mm", verified="cdfer-basic"))
_add(PartDef("HOLE_M3", "Mechanical:MountingHole", "MountingHole:MountingHole_3.2mm_M3", {},
             lcsc=None, jlc="Hand", value="M3", desc="Mounting hole"))
