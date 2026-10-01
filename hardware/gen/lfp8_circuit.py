"""LFP-8 board: complete netlist (single source of truth).

Used by
  * sim/spice_netlist.py      -> ngspice testbenches
  * hardware/gen/make_kicad.py -> schematic, netlist, PCB, BOM, CPL

Channel k (1..8) reference designators use the k*100 range (R101.., Q801..).
Board-level parts use numbers < 100.
"""
from circuit import Circuit
from parts import P, resistor as R, capacitor as C

N_CH = 8

# ----------------------------------------------------------------- tunables
# (values chosen with the ngspice testbenches in sim/tests)
CC_INT_R = "1k"      # CC error amp zero resistor
CC_INT_C = "47n"     # CC error amp integrator capacitor (and V+ balance cap)
CV_INT_R = "10k"     # CV error amp zero resistor
CV_INT_C = "10n"     # CV error amp integrator capacitor (and V+ balance cap)
BP_CAP = "1u"         # charger output capacitor (per channel)
G1_CAP = "1n"         # gate-source capacitor of the P-FET pass device
Q2_RE = "1k"          # emitter degeneration of the gate driver

# 74HC595 bit map (see docs/INTERFACE.md)
def sr_bit_net(bit: int) -> str:
    if bit < 24:
        k, f = bit // 3 + 1, bit % 3
        return [f"CHGEN_{k}", f"DISEN_{k}", f"LED_{k}"][f]
    return {24: "MUXS0", 25: "MUXS1", 26: "MUXS2", 27: "ISETB0", 28: "ISETB1",
            29: "ISETB2", 30: "AUX_OUT", 31: "NC_SR_QH4"}[bit]


SR_PINS = ["QA", "QB", "QC", "QD", "QE", "QF", "QG", "QH"]


def build_channel(c: Circuit, k: int):
    g = f"ch{k}"
    n = lambda s: f"{s}_{k}"
    r = lambda i: f"R{k}{i:02d}"
    cc = lambda i: f"C{k}{i:02d}"
    q = lambda i: f"Q{k}{i:02d}"
    d = lambda i: f"D{k}{i:02d}"

    BPF, BP, BPS, BNS, BN = n("BPF"), n("BP"), n("BPS"), n("BNS"), n("BN")
    # --- cell connector, PTC, Kelvin fall-back resistors, ESD/filter caps
    c.add(f"J{k}01", P["JST_XH_6"], {"1": BPF, "2": BPS, "3": BNS, "4": BN, "5": n("NTC"), "6": "GND"}, g)
    c.add(f"F{k}01", P["PTC_2A"], {"1": BPF, "2": BP}, g)
    c.add(cc(1), C("10n"), {"1": BPS, "2": "GND"}, g)
    c.add(cc(2), C("10n"), {"1": BNS, "2": "GND"}, g)
    c.add(r(1), R("100"), {"1": BPS, "2": BP}, g, note="Kelvin fall-back B+")
    c.add(r(2), R("100"), {"1": BNS, "2": BN}, g, note="Kelvin fall-back B-")
    c.add(cc(3), C(BP_CAP), {"1": BP, "2": "GND"}, g)

    # --- charge path: +5V -> Q01 (P-FET pass) -> D01 (reverse block) -> R06 (1.2 ohm ballast + CC sense) -> BP
    c.add(q(1), P["AO3401A"], {"S": "+5V", "G": n("G1"), "D": n("CH1")}, g)
    c.add(r(3), R("10k"), {"1": n("G1"), "2": "+5V"}, g)
    c.add(cc(9), C(G1_CAP), {"1": n("G1"), "2": "+5V"}, g, note="pass-stage bandwidth limit")
    c.add(q(2), P["MMBT3904"], {"C": n("G1"), "B": n("NBB"), "E": n("E2")}, g)
    c.add(r(4), R("4.7k"), {"1": n("NB"), "2": n("NBB")}, g)
    c.add(r(5), R(Q2_RE), {"1": n("E2"), "2": "GND"}, g)
    c.add(d(1), P["SS34"], {"A": n("CH1"), "K": n("CH2")}, g)
    c.add(r(6), P["R_2512_1R2"], {"1": n("CH2"), "2": BP}, g)
    c.add(r(7), R("10k"), {"1": n("CHGEN"), "2": n("NB")}, g)
    c.add(r(8), R("100k"), {"1": n("NB"), "2": "GND"}, g)
    c.add(d(2), P["BAT54WS"], {"A": n("NB"), "K": n("CCO")}, g)
    c.add(d(3), P["BAT54WS"], {"A": n("NB"), "K": n("CVO")}, g)
    c.add(q(3), P["2N7002"], {"D": n("NB"), "G": n("DDRV"), "S": "GND"}, g, note="charge/discharge interlock")

    # --- quad op-amp: A=CC error amp, B=CV error amp, C=discharge driver+UV, D=OV latch
    c.add(f"U{k}01", P["LM324"], {
        "IN1+": n("CCP"), "IN1-": n("CCN"), "OUT1": n("CCO"),
        "IN2+": n("CVP"), "IN2-": n("CVS"), "OUT2": n("CVO"),
        "IN3+": n("SUMD"), "IN3-": n("UVN"), "OUT3": n("DDRV"),
        "IN4+": n("OVP"), "IN4-": n("OVS"), "OUT4": n("OVO"),
        "V+": "+5V", "V-": "GND"}, g)
    c.add(cc(4), C("100n"), {"1": "+5V", "2": "GND"}, g)
    # CC error amp: V+ = (BP + VS)/3, V- = CH2/3  ->  regulates I_chg * 1.2 ohm = VS
    c.add(r(9), R("100k"), {"1": BP, "2": n("CCP")}, g)
    c.add(r(10), R("100k"), {"1": "VS", "2": n("CCP")}, g)
    c.add(r(11), R("100k"), {"1": n("CCP"), "2": "GND"}, g)
    c.add(r(12), R("100k"), {"1": n("CH2"), "2": n("CCN")}, g)
    c.add(r(13), R("49.9k"), {"1": n("CCN"), "2": "GND"}, g)
    c.add(r(14), R(CC_INT_R) if CC_INT_R != "0" else R("100"), {"1": n("CCO"), "2": n("CCZ")}, g)
    c.add(cc(5), C(CC_INT_C), {"1": n("CCZ"), "2": n("CCN")}, g)
    c.add(cc(10), C(CC_INT_C), {"1": n("CCP"), "2": "GND"}, g, note="balances the differential integrator (CM rejection)")
    c.add(d(7), P["BAT54WS"], {"A": n("CCP"), "K": n("CHGEN")}, g, note="anti wind-up / soft start")
    # CV error amp (differential, Kelvin): 0.3896*(B+S) vs 0.3914*(B-S) + 0.3807*VREF -> 3.570 V
    c.add(r(15), R("47k"), {"1": BPS, "2": n("CVS")}, g)
    c.add(r(16), R("30k"), {"1": n("CVS"), "2": "GND"}, g)
    c.add(r(41), R("47k"), {"1": BNS, "2": n("CVP")}, g)
    c.add(r(42), R("33k"), {"1": "VREF", "2": n("CVP")}, g)
    c.add(r(43), R("360k"), {"1": n("CVP"), "2": "GND"}, g)
    c.add(r(17), R(CV_INT_R), {"1": n("CVO"), "2": n("CVZ")}, g)
    c.add(cc(6), C(CV_INT_C), {"1": n("CVZ"), "2": n("CVS")}, g)
    c.add(cc(11), C(CV_INT_C), {"1": n("CVP"), "2": "GND"}, g, note="balances the differential integrator (CM rejection)")
    c.add(d(8), P["BAT54WS"], {"A": n("CVP"), "K": n("CHGEN")}, g, note="anti wind-up / soft start")
    # discharge driver with differential hardware UV cut-off (2.24 V under load, re-arm 2.59 V)
    c.add(r(18), R("100k"), {"1": BPS, "2": n("SUMD")}, g)
    c.add(r(19), R("75k"), {"1": n("DISEN"), "2": n("SUMD")}, g)
    c.add(r(20), R("36k"), {"1": n("SUMD"), "2": "GND"}, g)
    c.add(r(21), R("1M"), {"1": n("DDRV"), "2": n("SUMD")}, g)
    c.add(r(44), R("100k"), {"1": BNS, "2": n("UVN")}, g)
    c.add(r(45), R("27k"), {"1": "VREF", "2": n("UVN")}, g)
    c.add(r(46), R("200k"), {"1": n("UVN"), "2": "GND"}, g)
    c.add(r(22), R("100"), {"1": n("DDRV"), "2": n("DG")}, g)
    c.add(r(23), R("100k"), {"1": n("DG"), "2": "GND"}, g)
    c.add(q(4), P["AO3400A"], {"D": n("DLOAD"), "G": n("DG"), "S": "GND"}, g)
    for i in range(4):
        c.add(r(24 + i), P["R_2512_11R"], {"1": BP, "2": n("DLOAD")}, g)
    # OV latch (differential): trips at 3.80 V, releases only below 2.74 V (cell removed).
    #   V- = 0.3267*B+S ; V+ = 0.3268*B-S + 0.5414*VREF*... + hysteresis from OVO (VOH ~3.75 V @ 5.2 V)
    c.add(r(28), R("68k"), {"1": BPS, "2": n("OVS")}, g)
    c.add(r(29), R("33k"), {"1": n("OVS"), "2": "GND"}, g)
    c.add(cc(7), C("1u"), {"1": n("OVS"), "2": "GND"}, g)
    c.add(r(47), R("68k"), {"1": BNS, "2": n("OVP")}, g)
    c.add(r(30), R("62k"), {"1": "VREF", "2": n("OVP")}, g)
    c.add(r(48), R("100k"), {"1": n("OVP"), "2": "GND"}, g)
    c.add(r(31), R("240k"), {"1": n("OVO"), "2": n("OVP")}, g)
    c.add(d(4), P["BAT54WS"], {"A": n("GEN"), "K": n("OVO")}, g)
    c.add(d(9), P["BAT54WS"], {"A": n("OVS"), "K": "SAFE_EN"}, g, note="OV latch reset while SAFE_EN is low")

    # --- low-side isolation: back-to-back MOSFETs (block both directions when off).
    #     Gate drive is referenced to their common source (MIDISO): a PNP switch pulls the
    #     gates to +5 V only when GEN (gate enable, wired-AND below) is high; otherwise
    #     R53 (22k) holds Vgs = 0 even when MIDISO swings negative or the board is unpowered.
    c.add(q(5), P["AO3400A"], {"D": BN, "G": n("GISO"), "S": n("MIDISO")}, g)
    c.add(q(8), P["AO3400A"], {"D": n("SHP"), "G": n("GISO"), "S": n("MIDISO")}, g)
    c.add(r(53), R("22k"), {"1": n("GISO"), "2": n("MIDISO")}, g)
    c.add(q(9), P["MMBT3906"], {"E": "+5V", "B": n("GPB"), "C": n("GISO")}, g, note="isolation gate high-side switch")
    c.add(r(51), R("47k"), {"1": "+5V", "2": n("GPB")}, g)
    c.add(q(10), P["MMBT3904"], {"C": n("GPB"), "B": n("GENB"), "E": "GND"}, g)
    c.add(r(52), R("47k"), {"1": n("GEN"), "2": n("GENB")}, g)
    # GEN = SAFE_EN AND (not OV latched) AND (not reversed)   (diode / open-collector wired-AND)
    c.add(r(32), R("100k"), {"1": "+5V", "2": n("GEN")}, g)
    c.add(d(5), P["BAT54WS"], {"A": n("GEN"), "K": "SAFE_EN"}, g)
    c.add(d(10), P["BAT54WS"], {"A": n("NB"), "K": n("GEN")}, g, note="isolation also kills charger drive")
    # reverse-polarity sentinel (two halves sharing one threshold divider, 150k/91k:
    # trips when B- is > ~1.6 V above B+, an empty slot (~1 V) is ignored):
    #  * Q06 NPN, emitter on B+  -> pulls GEN down when B+ is driven negative by the cell
    #  * Q07 PNP + Q11 NPN       -> pulls GEN to ground when the charger lifts B+ positive
    c.add(r(33), R("150k"), {"1": BN, "2": n("SNB")}, g)
    c.add(r(55), R("91k"), {"1": n("SNB"), "2": BP}, g)
    c.add(q(6), P["MMBT3904"], {"B": n("SNB"), "E": BP, "C": n("GEN")}, g, note="reverse-polarity sentinel A")
    c.add(q(7), P["MMBT3906"], {"E": n("SNB"), "B": n("SNPB"), "C": n("SNC")}, g, note="reverse-polarity sentinel B")
    c.add(r(34), R("47k"), {"1": n("SNPB"), "2": BP}, g)
    c.add(q(11), P["MMBT3904"], {"B": n("SNC"), "E": "GND", "C": n("GEN")}, g)
    c.add(r(49), R("1M"), {"1": n("SNC"), "2": "GND"}, g)
    c.add(f"R{k}50", P["R_SHUNT_0R1"], {"1": n("SHP"), "2": "GND", "S1": n("SHPK"), "S2": n("SHNK")}, g)

    # --- measurement taps (to the multiplexers)
    c.add(r(35), R("1k"), {"1": BPS, "2": n("MXA")}, g)
    c.add(r(36), R("1k"), {"1": BNS, "2": n("MXB")}, g)
    c.add(r(37), R("1k"), {"1": n("SHPK"), "2": n("MXC")}, g)
    c.add(r(38), R("1k"), {"1": n("SHNK"), "2": n("MXE")}, g)
    c.add(r(39), R("10k"), {"1": "+3V3", "2": n("NTC")}, g)
    c.add(cc(8), C("10n"), {"1": n("NTC"), "2": "GND"}, g)
    # --- LED
    c.add(r(40), R("1k"), {"1": n("LED"), "2": n("LEDA")}, g)
    c.add(d(6), P["LED_G"], {"A": n("LEDA"), "K": "GND"}, g)


def build_board() -> Circuit:
    c = Circuit("LFP-8")
    for k in range(1, N_CH + 1):
        build_channel(c, k)

    # ============================================================ power
    g = "power"
    c.add("J1", P["TERM_2"], {"1": "+5V", "2": "GND"}, g)
    c.add("D1", P["SMBJ6.0A"], {"K": "+5V", "A": "GND"}, g)
    c.add("D2", P["SS54"], {"K": "+5V", "A": "GND"}, g, note="reverse-polarity crowbar")
    c.add("C1", C("100u", "1206"), {"1": "+5V", "2": "GND"}, g)
    c.add("C2", C("100u", "1206"), {"1": "+5V", "2": "GND"}, g)
    c.add("C3", C("22u", "1206"), {"1": "+5V", "2": "GND"}, g)
    c.add("C4", C("22u", "1206"), {"1": "+5V", "2": "GND"}, g)
    c.add("U1", P["AMS1117-3.3"], {"VI": "+5V", "VO": "+3V3", "GND": "GND"}, g)
    c.add("C5", C("22u", "0805"), {"1": "+5V", "2": "GND"}, g)
    c.add("C6", C("22u", "0805"), {"1": "+3V3", "2": "GND"}, g)
    c.add("C7", C("100n"), {"1": "+3V3", "2": "GND"}, g)
    c.add("U2", P["CJ431"], {"K": "VREF", "REF": "VREF", "A": "GND"}, g)
    c.add("R1", R("1k"), {"1": "+5V", "2": "VREF"}, g)
    c.add("C8", C("10u", "0805"), {"1": "VREF", "2": "GND"}, g)
    # charge-current DAC (3 bit, binary weighted from the 5 V shift-register outputs) + buffer
    c.add("R4", R("10k"), {"1": "ISETB2", "2": "VSRAW"}, g)
    c.add("R5", R("20k"), {"1": "ISETB1", "2": "VSRAW"}, g)
    c.add("R6", R("40.2k"), {"1": "ISETB0", "2": "VSRAW"}, g)
    c.add("R7", R("4.3k"), {"1": "VSRAW", "2": "GND"}, g)
    c.add("U3", P["LM358"], {"IN1+": "VSRAW", "IN1-": "VS", "OUT1": "VS",
                             "IN2+": "GND", "IN2-": "U3B_OUT", "OUT2": "U3B_OUT",
                             "V+": "+5V", "V-": "GND"}, g)
    c.add("C10", C("100n"), {"1": "+5V", "2": "GND"}, g)
    c.add("C11", C("100n"), {"1": "VS", "2": "GND"}, g)

    # ============================================================ safety chain (SAFE_EN)
    g = "safety"
    c.add("U4", P["LM393"], {"IN1+": "PQ", "IN1-": "WDTH", "OUT1": "SAFE_EN",
                             "IN2+": "VINS1", "IN2-": "VREF", "OUT2": "SAFE_EN",
                             "V+": "+5V", "V-": "GND"}, g)
    c.add("C12", C("100n"), {"1": "+5V", "2": "GND"}, g)
    c.add("U5", P["LM393"], {"IN1+": "VREF", "IN1-": "VINS2", "OUT1": "SAFE_EN",
                             "IN2+": "TS", "IN2-": "OTTH", "OUT2": "SAFE_EN",
                             "V+": "+5V", "V-": "GND"}, g)
    c.add("C13", C("100n"), {"1": "+5V", "2": "GND"}, g)
    # watchdog charge pump: HEARTBEAT square wave (>=100 Hz) -> PQ; decays in ~35 ms when it stops
    c.add("C14", C("100n"), {"1": "HEARTBEAT", "2": "PP"}, g)
    c.add("D7", P["BAT54WS"], {"A": "GND", "K": "PP"}, g)
    c.add("D8", P["BAT54WS"], {"A": "PP", "K": "PQ"}, g)
    c.add("C15", C("470n"), {"1": "PQ", "2": "GND"}, g)
    c.add("R8", R("100k"), {"1": "PQ", "2": "GND"}, g)
    c.add("R9", R("10k"), {"1": "VREF", "2": "WDTH"}, g)
    c.add("R10", R("9.1k"), {"1": "WDTH", "2": "GND"}, g)
    # rail UV (4.54 V) with ~40 mV hysteresis, rail OV (5.49 V)
    c.add("R11", R("8.2k"), {"1": "+5V", "2": "VINS1"}, g)
    c.add("R12", R("10k"), {"1": "VINS1", "2": "GND"}, g)
    c.add("R46", R("1M"), {"1": "SAFE_EN", "2": "VINS1"}, g)
    c.add("C16", C("100n"), {"1": "VINS1", "2": "GND"}, g)
    c.add("R13", R("12k"), {"1": "+5V", "2": "VINS2"}, g)
    c.add("R14", R("10k"), {"1": "VINS2", "2": "GND"}, g)
    c.add("C17", C("100n"), {"1": "VINS2", "2": "GND"}, g)
    # board over-temperature: diode-connected MMBT3904 (-2.1 mV/K), trip ~ 80 C
    c.add("Q7", P["MMBT3904"], {"B": "TS", "C": "TS", "E": "GND"}, g)
    c.add("R15", R("47k"), {"1": "+5V", "2": "TS"}, g)
    c.add("C18", C("100n"), {"1": "TS", "2": "GND"}, g)
    c.add("R16", R("39k"), {"1": "VREF", "2": "OTTH"}, g)
    c.add("R17", R("10k"), {"1": "OTTH", "2": "GND"}, g)
    c.add("R18", R("4.7k"), {"1": "+5V", "2": "SAFE_EN"}, g)
    c.add("SW1", P["SW_TACT"], {"1": "SAFE_EN", "2": "GND"}, g, value="E-STOP")
    c.add("Q8", P["2N7002"], {"G": "SAFE_EN", "S": "GND", "D": "OE_N"}, g)
    c.add("R20", R("10k"), {"1": "+5V", "2": "OE_N"}, g)
    c.add("R21", R("1k"), {"1": "+5V", "2": "LEDS_A"}, g)
    c.add("D9", P["LED_G"], {"A": "LEDS_A", "K": "OE_N"}, g, value="SAFE")
    c.add("R22", R("22k"), {"1": "SAFE_EN", "2": "SAFE_RB"}, g)
    c.add("R23", R("33k"), {"1": "SAFE_RB", "2": "GND"}, g)
    c.add("R24", R("2.2k"), {"1": "+3V3", "2": "LEDP_A"}, g)
    c.add("D10", P["LED_R"], {"A": "LEDP_A", "K": "GND"}, g, value="PWR")

    # ============================================================ MCU
    g = "mcu"
    c.add("U6", P["ESP32-C3-WROOM-02"], {
        "3V3": "+3V3", "EN": "EN", "IO4": "TS", "IO5": "SDA3", "IO6": "SCL3", "IO7": "SER3",
        "IO8": "SRCLK3", "IO9": "BOOT", "GND": "GND", "IO10": "HEARTBEAT", "IO20": "SAFE_RB",
        "IO21": "FAN_PWM", "IO18": "USB_DN", "IO19": "USB_DP", "IO3": "ADC_VIN", "IO2": "RCLK3",
        "IO1": "ADC_VCHK", "IO0": "MUXD", "EPAD": "GND"}, g)
    c.add("C19", C("22u", "0805"), {"1": "+3V3", "2": "GND"}, g)
    c.add("C20", C("100n"), {"1": "+3V3", "2": "GND"}, g)
    c.add("R25", R("10k"), {"1": "+3V3", "2": "EN"}, g)
    c.add("C21", C("1u"), {"1": "EN", "2": "GND"}, g)
    c.add("SW2", P["SW_TACT"], {"1": "EN", "2": "GND"}, g, value="RESET")
    c.add("R26", R("10k"), {"1": "+3V3", "2": "BOOT"}, g)
    c.add("SW3", P["SW_TACT"], {"1": "BOOT", "2": "GND"}, g, value="BOOT")
    c.add("J3", P["HDR_6"], {"1": "VBUS", "2": "USB_DN", "3": "USB_DP", "4": "GND", "5": "EN", "6": "BOOT"}, g,
          value="USB/PROG")
    c.add("D11", P["SS14"], {"A": "VBUS", "K": "+5V"}, g)
    c.add("J4", P["HDR_6"], {"1": "+5V", "2": "GND", "3": "AUX_OUT", "4": "SDA3", "5": "SCL3", "6": "+3V3"}, g,
          value="AUX")
    # fan
    c.add("J2", P["HDR_2"], {"1": "+5V", "2": "FAN_N"}, g, value="FAN 5V")
    c.add("Q14", P["AO3400A"], {"D": "FAN_N", "G": "FANG", "S": "GND"}, g)
    c.add("R44", R("100"), {"1": "FAN_PWM", "2": "FANG"}, g)
    c.add("R45", R("100k"), {"1": "FANG", "2": "GND"}, g)
    c.add("D12", P["SS14"], {"A": "FAN_N", "K": "+5V"}, g)

    # ============================================================ ADC + multiplexers
    g = "adc"
    c.add("U7", P["ADS1115"], {"ADDR": "GND", "ALERT": "NC_ADS_ALERT", "GND": "GND", "AIN0": "MUXA",
                               "AIN1": "MUXB", "AIN2": "MUXC", "AIN3": "MUXE", "VDD": "+5V",
                               "SDA": "SDA5", "SCL": "SCL5"}, g)
    c.add("C22", C("100n"), {"1": "+5V", "2": "GND"}, g)
    mux_sig = {"U8": ("MXA", "MUXA"), "U9": ("MXB", "MUXB"), "U10": ("MXC", "MUXC"),
               "U11": ("NTC", "MUXD"), "U12": ("MXE", "MUXE")}
    cap_ref = 23
    for ref, (sig, out) in mux_sig.items():
        conns = {f"X{i}": f"{sig}_{i + 1}" for i in range(8)}
        conns.update({"X": out, "A": "MUXS0", "B": "MUXS1", "C": "MUXS2", "INH": "GND",
                      "VEE": "GND", "VSS": "GND", "VDD": "+5V"})
        c.add(ref, P["CD4051B"], conns, g)
        c.add(f"C{cap_ref}", C("100n"), {"1": "+5V", "2": "GND"}, g); cap_ref += 1
        c.add(f"C{cap_ref}", C("10n"), {"1": out, "2": "GND"}, g); cap_ref += 1
    # cap_ref now 33
    c.add("R27", R("1M"), {"1": "MUXA", "2": "ADC_VCHK"}, g)
    c.add("R28", R("1M"), {"1": "ADC_VCHK", "2": "GND"}, g)
    c.add("C33", C("10n"), {"1": "ADC_VCHK", "2": "GND"}, g)
    c.add("R29", R("100k"), {"1": "+5V", "2": "ADC_VIN"}, g)
    c.add("R30", R("47k"), {"1": "ADC_VIN", "2": "GND"}, g)
    c.add("C34", C("100n"), {"1": "ADC_VIN", "2": "GND"}, g)
    for i, s in enumerate(["MUXS0", "MUXS1", "MUXS2"]):
        c.add(f"R{31 + i}", R("100k"), {"1": s, "2": "GND"}, g)

    # ============================================================ logic: shift registers + level shifters
    g = "logic"
    ser_in = "SER5"
    for chip in range(4):
        ref = f"U{13 + chip}"
        out = f"SRCH{chip + 1}" if chip < 3 else "NC_SR_QH_OUT"
        conns = {"SER": ser_in, "QH'": out, "SRCLK": "SRCLK5", "RCLK": "RCLK5", "OE": "OE_N",
                 "SRCLR": "+5V", "VCC": "+5V", "GND": "GND"}
        for qi, qn in enumerate(SR_PINS):
            conns[qn] = sr_bit_net(chip * 8 + qi)
        c.add(ref, P["74HC595"], conns, g)
        c.add(f"C{35 + chip}", C("100n"), {"1": "+5V", "2": "GND"}, g)
        ser_in = out
    shifters = [("Q9", "SDA", "4.7k"), ("Q10", "SCL", "4.7k"), ("Q11", "SER", "10k"),
                ("Q12", "SRCLK", "10k"), ("Q13", "RCLK", "10k")]
    rr = 34
    for ref, sig, pull in shifters:
        c.add(ref, P["BSS138"], {"G": "+3V3", "S": f"{sig}3", "D": f"{sig}5"}, g)
        c.add(f"R{rr}", R(pull), {"1": "+3V3", "2": f"{sig}3"}, g); rr += 1
        c.add(f"R{rr}", R(pull), {"1": "+5V", "2": f"{sig}5"}, g); rr += 1
    # rr == 44
    for i in range(4):
        c.add(f"H{i + 1}", P["HOLE_M3"], {}, "mech")
    return c


# nets that legitimately have one connection
SINGLE_OK = {"NC_ADS_ALERT", "NC_SR_QH4", "NC_SR_QH_OUT"}

if __name__ == "__main__":
    b = build_board()
    probs = b.check(SINGLE_OK)
    print(f"{len(b.insts)} parts, {len(b.nets())} nets")
    for p in probs:
        print("  ", p)
