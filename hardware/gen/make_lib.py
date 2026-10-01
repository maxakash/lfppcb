"""Generate the project library 'LFP8' (KiCad 7 format):
   symbols:    ESP32-C3-WROOM-02
   footprints: R_2512_Kelvin (net-tie Kelvin shunt), SW_SMD_5.1x5.1_4P
"""
import os
import uuid

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "kicad", "lib")


def u():
    return str(uuid.uuid4())


ESP_LEFT = [("1", "3V3", "power_in"), ("2", "EN", "input"), ("18", "IO0", "bidirectional"),
            ("17", "IO1", "bidirectional"), ("16", "IO2", "bidirectional"), ("15", "IO3", "bidirectional"),
            ("3", "IO4", "bidirectional"), ("4", "IO5", "bidirectional"), ("5", "IO6", "bidirectional"),
            ("6", "IO7", "bidirectional")]
ESP_RIGHT = [("7", "IO8", "bidirectional"), ("8", "IO9", "bidirectional"), ("10", "IO10", "bidirectional"),
             ("13", "IO18/USB_D-", "bidirectional"), ("14", "IO19/USB_D+", "bidirectional"),
             ("11", "IO20/RXD", "bidirectional"), ("12", "IO21/TXD", "bidirectional"),
             ("9", "GND", "power_in"), ("19", "GND(EPAD)", "passive")]


def esp_symbol(prefix=""):
    name = "ESP32-C3-WROOM-02"
    h = max(len(ESP_LEFT), len(ESP_RIGHT)) * 2.54 + 2.54
    top = h / 2
    pins = []
    for i, (num, nm, typ) in enumerate(ESP_LEFT):
        y = top - 2.54 * (i + 1)
        pins.append(f'      (pin {typ} line (at -15.24 {y:.2f} 0) (length 2.54) (name "{nm}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
    for i, (num, nm, typ) in enumerate(ESP_RIGHT):
        y = top - 2.54 * (i + 1)
        pins.append(f'      (pin {typ} line (at 15.24 {y:.2f} 180) (length 2.54) (name "{nm}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
    return f'''  (symbol "{prefix}{name}" (in_bom yes) (on_board yes)
    (property "Reference" "U" (at -12.7 {top + 2.54:.2f} 0) (effects (font (size 1.27 1.27)) (justify left)))
    (property "Value" "{name}" (at -12.7 {-top - 2.54:.2f} 0) (effects (font (size 1.27 1.27)) (justify left)))
    (property "Footprint" "RF_Module:ESP32-C3-WROOM-02" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "https://www.espressif.com/sites/default/files/documentation/esp32-c3-wroom-02_datasheet_en.pdf" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (property "ki_description" "ESP32-C3 Wi-Fi/BLE module, 4 MB flash, PCB antenna" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (symbol "{name}_0_1"
      (rectangle (start -12.7 {top:.2f}) (end 12.7 {-top:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "{name}_1_1"
{chr(10).join(pins)}
    )
  )'''


def write_symbols():
    os.makedirs(OUT, exist_ok=True)
    txt = "(kicad_symbol_lib (version 20220914) (generator lfp8_gen)\n" + esp_symbol() + "\n)\n"
    open(os.path.join(OUT, "LFP8.kicad_sym"), "w").write(txt)


def fp_kelvin():
    # Standard 2512 land pattern (IPC nominal) + two small sense pads attached to the
    # inner edge of each force pad.  net_tie_pad_groups lets DRC accept the joint.
    return f'''(footprint "R_2512_Kelvin" (version 20221018) (generator lfp8_gen)
  (layer "F.Cu")
  (descr "2512 current-sense resistor with Kelvin sense pads (net tie). Pads 1/2 force, 3 = sense of 1, 4 = sense of 2")
  (tags "resistor shunt kelvin 2512")
  (fp_text reference "REF**" (at 0 -3.2) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text value "R_2512_Kelvin" (at 0 3.2) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (attr smd)
  (net_tie_pad_groups "1, 3" "2, 4")
  (fp_line (start -2.177 -1.71) (end 2.177 -1.71) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_line (start -2.177 1.71) (end 2.177 1.71) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_rect (start -3.82 -2.4) (end 3.82 2.4) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
  (fp_rect (start -3.15 -1.6) (end 3.15 1.6) (stroke (width 0.1) (type solid)) (fill none) (layer "F.Fab"))
  (fp_text user "${{REFERENCE}}" (at 0 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (pad "1" smd roundrect (at -2.9625 -0.45) (size 1.225 2.45) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2))
  (pad "2" smd roundrect (at 2.9625 -0.45) (size 1.225 2.45) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2))
  (pad "3" smd rect (at -2.75 1.55) (size 0.8 0.6) (layers "F.Cu" "F.Mask"))
  (pad "4" smd rect (at 2.75 1.55) (size 0.8 0.6) (layers "F.Cu" "F.Mask"))
  (fp_poly (pts (xy -3.15 0.7) (xy -2.35 0.7) (xy -2.35 1.3) (xy -3.15 1.3)) (stroke (width 0) (type solid)) (fill solid) (layer "F.Cu"))
  (fp_poly (pts (xy 2.35 0.7) (xy 3.15 0.7) (xy 3.15 1.3) (xy 2.35 1.3)) (stroke (width 0) (type solid)) (fill solid) (layer "F.Cu"))
  (model "${{KICAD6_3DMODEL_DIR}}/Resistor_SMD.3dshapes/R_2512_6332Metric.wrl" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))
)
'''


def fp_switch():
    # JLCPCB C318884 (5.1 x 5.1 mm, 4 pads at +-3.3, +-1.85).  The two electrical
    # pads are diagonal so the part works whatever its internal pin pairing is;
    # the other two pads are mechanical (no net).
    return '''(footprint "SW_SMD_5.1x5.1_4P" (version 20221018) (generator lfp8_gen)
  (layer "F.Cu")
  (descr "Tactile switch 5.1x5.1 mm SMD (JLCPCB C318884). Diagonal pads 1/2 carry the circuit, the others are mechanical")
  (tags "switch tactile SMD")
  (fp_text reference "REF**" (at 0 -3.6) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text value "SW_SMD_5.1x5.1_4P" (at 0 3.6) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (attr smd)
  (fp_rect (start -2.55 -2.55) (end 2.55 2.55) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))
  (fp_circle (center 0 0) (end 1.3 0) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))
  (fp_rect (start -4.4 -2.9) (end 4.4 2.9) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
  (fp_text user "${REFERENCE}" (at 0 0) (layer "F.Fab") (effects (font (size 0.8 0.8) (thickness 0.12))))
  (pad "1" smd roundrect (at -3.3 -1.85) (size 1.7 1) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
  (pad "" smd roundrect (at 3.3 -1.85) (size 1.7 1) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
  (pad "" smd roundrect (at -3.3 1.85) (size 1.7 1) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
  (pad "2" smd roundrect (at 3.3 1.85) (size 1.7 1) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15))
)
'''


def write_footprints():
    d = os.path.join(OUT, "LFP8.pretty")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "R_2512_Kelvin.kicad_mod"), "w").write(fp_kelvin())
    open(os.path.join(d, "SW_SMD_5.1x5.1_4P.kicad_mod"), "w").write(fp_switch())


if __name__ == "__main__":
    write_symbols()
    write_footprints()
    print("library written to", os.path.abspath(OUT))
