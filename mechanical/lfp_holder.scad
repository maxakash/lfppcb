// =============================================================================
//  lfp_holder.scad  -  parametric 3D-printable holder system for 40 x LiFePO4
//                      33140 cells, tested by five LFP-8 boards (8 ch each)
// -----------------------------------------------------------------------------
//  Target printer : Creality Ender 3 S1 Pro (220 x 220 x 270 mm bed); every part
//                   is kept inside 210 x 210 mm footprint and <= 260 mm height.
//  Tool           : OpenSCAD 2021.01 (no libraries, no fonts needed - all
//                   lettering is drawn as 7-segment glyphs).
//
//  PARTS (select with  -D 'part="..."'):
//    "cradle"      4-cell horizontal cradle (default). 10 are needed for 40 cells.
//                  Two cradles stack (pins/sockets) = one 8-cell module per LFP-8.
//                  -D label_offset=0 -> channels 1..4, label_offset=4 -> 5..8
//    "board_stand" vertical stand for one LFP-8 PCB (4x M3 standoffs) with a
//                  60/80 mm 5 V fan underneath blowing UP across both PCB faces.
//    "board_tray"  (accessory) flat tray that sits on top of a stacked module
//                  using the same stacking pins; PCB flat, fan blows across it.
//    "pogo_gauge"  (accessory) depth gauge to set the pogo-pin protrusion.
//    "assembly"    visual check only: 2 cradles + dummy cells + board stand.
//    "section"     visual check only: cut through one cell with plates, spring,
//                  pogo pins and NTC bead in place.
//
//  COORDINATE SYSTEM OF THE CRADLE (print orientation = use orientation):
//    X = cell axis.  x = 0 is the outer face of the wire raceway at the
//        POSITIVE ("connector") end; the NEGATIVE (spring) end is at +X.
//    Y = across the cells, cell i (0..n-1) is centred at y = (i + 0.5)*pitch.
//    Z = up, z = 0 is the print bed / table.
//    P (variable below) is the "positive contact plane" = face of the positive
//    force plate; every axial dimension of the cell is referenced to P.
//
//  KELVIN CONTACT SCHEME PER CELL
//    + end : flat 15x15 force plate (B+F) in a pocket, pogo pin (B+S) 6.0 mm
//            below the cell axis (lands on the raised + cap, clear of any M4/M5
//            thread), plate shifted 4.0 mm up so plate and pogo never touch.
//    - end : 15x15 plate with conical spring (B-F) in a captured slot, pogo pin
//            (B-S) 6.0 mm below the axis on the flat can bottom.
//    NTC   : pocket in the saddle under the middle of the cell.
//    The spring pushes every cell against the + plate, so the + end is the
//    fixed datum; cell-length tolerance is taken by the spring (and by the
//    4 mm working stroke of the - end pogo pin).
// =============================================================================

/* [Part selection] */
part = "cradle";        // [cradle, board_stand, board_tray, pogo_gauge, assembly, section]
label_offset = 0;       // 0 -> labels 1..4 ; 4 -> labels 5..8 (second cradle of a module)

/* [Printer] */
bed_x = 220;            // Ender 3 S1 Pro bed
bed_y = 220;
bed_z = 270;
bed_margin = 5;         // per side -> usable 210 x 210
z_margin = 10;          // -> usable height 260
nozzle = 0.4;
min_wall = 2.4;         // 3 perimeters x 0.4 mm x 2 (requirement)

/* [Cell (33140 LiFePO4, 15 Ah)] */
cell_d_nom = 33.0;
cell_d_max = 33.6;      // design envelope
cell_len_min = 139.5;   // overall length incl. + cap
cell_len_nom = 140.0;
cell_len_max = 141.5;
cap_d_min = 14;         // raised + cap (terminal) diameter range
cap_d_max = 16;
cap_h_min = 0.5;        // raised + cap height range 0.5..1 mm
cell_clr = 0.4;         // radial clearance: saddle radius - max cell radius (>= 0.3)
cell_mass_g = 290;

/* [Cradle layout] */
n_cells = 4;            // cells per cradle
pitch = 48;             // cell centre distance -> 14 mm finger gap between cells
base_t = 3;             // floor thickness (>= min_wall)
wire_gap = 5;           // floor top -> lowest point of the saddle (wires run here)
stack_gap = 3.4;        // max-cell top -> underside of the cradle stacked above
rib_t = 5;              // saddle rib thickness (X)
ntc_rib_t = 9;          // the middle rib carries the NTC pocket (2.4 mm walls around it)
rib_frac = [0.16, 0.5, 0.84];  // rib positions as fraction of cell_len_nom from P
ntc_rib_index = 1;
rib_drop = 8;           // rib top this far below the saddle axis (saddle wraps +-62 deg)
saddle_wing = 2.4;      // material beside the saddle at rib top
rib_link_h = 8;         // low link between saddles of a rib (leaves finger gap)
strip_w = 21;           // floor strip under each cell (wire bed)
strip_slot_w = 7;       // ventilation slot along the middle of the strip (middle bays)
rail_t = 2.4;           // side rails
rail_h = 12;
web_h = 14;             // height of the end-wall web between the contact towers
web_t = 4;              // web thickness (at the back side of each end wall)
tower_w_pogo = 26;      // contact tower width, sense_style = "pogo"
tower_w_plate = 32;     // contact tower width, sense_style = "plate"
ch_top = 2;             // 45 deg chamfer on tower top edges (cell lead-in)
ch_rib = 1.2;           // 45 deg chamfer on rib lips

/* [Force contacts] */
fp_w = 15;              // force plate width  (standard nickel-plated steel plate)
fp_h = 15;              // force plate height
fp_t = 0.4;             // nominal plate thickness (0.3..0.5)
fp_t_tol = 0.1;         // +- thickness tolerance used in the spring calculation
fp_clr = 0.2;           // side clearance of plate in pocket / slot
tab_notch_w = 8;        // notch for the solder tab (tab goes UP, bent 90 deg back)
pos_notch_drop = 0;     // + : notch floor = plate top edge -> plate hangs on its bent tab
neg_notch_drop = -0.7;  // - : plate sits on the slot floor, bent tab clears the notch floor
force_off = 4.0;        // force plate centre above cell axis (pogo style)
spring_free_min = 8;    // conical spring free height range 8..10 mm
spring_free_max = 10;
spring_solid = 3;       // fully compressed height
spring_preload_min = 2; // minimum compression for the SHORTEST cell
spring_base_d = 12;     // large diameter of the conical spring
pocket_d = 0.6;         // + plate pocket depth (plate face 0.1..0.3 behind wall face)
cap_clear_d = 17.5;     // recess around the + cap (cap 16 max + axis tolerance)
neg_skin = 2.4;         // front skin of the - slot (holds plate edges)
neg_slot_t = 0.9;       // - slot thickness (plate 0.3..0.5 + clearance)
neg_window_w = 13;      // window in the skin for the spring
pos_wall_t = 8;         // + end wall (contact tower) thickness
neg_back_t = 6;         // - end wall material behind the slot

/* [Sense contacts] */
sense_style = "pogo";   // [pogo, plate]
sense_off = 6.0;        // pogo centre below the cell axis (lands on + cap / - can)
pogo_hole_d = 1.40;     // press fit for P100 barrel 1.36 (drill/ream 1.40 after print;
                        //  1.70 for R100 receptacles)
pogo_guide_d = 1.40;    // bore in the support fin (same drill as the wall: ream both in one pass)
pogo_head_clear_d = 2.2;// clearance for the 1.5 mm plunger head
pogo_len = 33.35;       // P100 overall length (plunger free)
pogo_ext = 6.85;        // plunger tip beyond barrel front, free (P100: full stroke 6.2..6.5)
pogo_work = 4.0;        // recommended working travel of P100
pogo_full = 6.2;        // full travel
pos_pogo_preload = 1.0; // + pogo tip stands this far in front of the + plate face
neg_pogo_preload = 0.5; // - pogo compression for the SHORTEST cell
sp_w = 10;              // sense plate (sense_style = "plate")
sp_h = 10;
sp_gap = 1.6;           // plastic / air gap between force and sense plate
sp_spring_base_d = 7;   // small spring on the - sense plate
sp_notch_w = 5;
sp_window_w = 8;

/* [Wire management] */
race_w_min = 20;        // raceway inner width; with pogo pins it grows so that the
tail_clear = 2.5;       //  pogo tail ends tail_clear before the outer lip (solder room)
race_lip_t = 2.4;
race_lip_h = 24;
exit_w = 10;            // U-notch where each cell's 6-wire bundle leaves
exit_floor = 8;
fin_t = 6.2;            // pogo support fin (2.4 mm walls around the bore)
fin_len = 15;
fin_above = 2.6;        // fin top above pogo axis
tie_w = 2.0;            // zip-tie slot through the fins (for 2.5 mm ties)
tie_h = 3.2;
tunnel_w = 5;           // wire tunnels / rib notches (bridge <= 10 mm)
tunnel_h = 3.5;
wire_notch_off = 8;     // tunnels at cell centre +- this (2.4 mm under the saddle)
ntc_d = 4.2;            // NTC bead pocket (bead <= 3 mm + epoxy)
ntc_depth = 5.5;        // pocket depth below saddle bottom (bead + 3 mm foam)
ntc_lead_w = 1.6;

/* [Stacking] */
post = 12;              // corner post size
pin_d = 6;
pin_h = 5;
stack_clr = 0.2;        // radial clearance pin/socket

/* [Labels] */
label_depth = 0.6;      // deboss depth
lip_digit_h = 10;
top_digit_h = 8;

/* [Board stand] */
pcb_w = 146;            // LFP-8 PCB 216 x 146 mm, mounted portrait: short side horizontal
pcb_h = 216;            // long side vertical (fan air flows along the 8 power stages)
pcb_t = 1.6;
pcb_hole_inset = 4.5;   // hole centre from each edge (4 x M3 at the PCB corners)
standoff_h = 8;         // PCB back face to upright face
standoff_d = 7;
screw_pilot_d = 2.8;    // M3 self-tapping into PETG (4.0 for M3 heat-set inserts)
fan_size = 80;          // 60 or 80 (40/92/120 also known)
fan_hole_spacing = 0;   // 0 = automatic from fan_size (60->50, 80->71.5)
fan_t = 15;             // fan thickness (80 x 15 slim fan keeps the stand < 260 mm tall)
fan_dx = 20;            // fan offset towards the PCB's JST / power-stage edge (clears the upright)
fan_gap = 10;           // air intake gap below the fan
stand_base_t = 3.2;
upright_w = 12;
upright_d = 14;

/* [Tray accessory] */
tray_t = 4;

/* [Quality] */
fn_saddle = 180;
fn_small = 24;

// ============================================================ derived values
max_x = bed_x - 2*bed_margin;
max_y = bed_y - 2*bed_margin;
max_z = bed_z - z_margin;

is_pogo  = (sense_style == "pogo");
r_s    = cell_d_max/2 + cell_clr;               // saddle radius
z_sb   = base_t + wire_gap;                     // saddle bottom
z_axs  = z_sb + r_s;                            // saddle axis
z_cell = z_sb + (cell_d_nom + cell_d_max)/4;    // axis of a resting mid-size cell
H      = z_sb + cell_d_max + stack_gap;         // cradle height = stack pitch

// spring calculation: shortest cell + thinnest plates still gets
// spring_preload_min with the shortest spring; see check_printability.py
L_contact = cell_len_min + spring_free_min - spring_preload_min - 2*fp_t_tol;

race_w     = is_pogo ? max(race_w_min, pogo_len - pos_pogo_preload + tail_clear
                                       - pos_wall_t + pocket_d - fp_t) : race_w_min;
W_pos      = race_lip_t + race_w + pos_wall_t;  // + wall inner face
x_pb       = W_pos - pos_wall_t;                // + wall back face
P          = W_pos - pocket_d + fp_t;           // + contact plane
x_spring   = P + L_contact;                     // - plate front face = spring base
x_ns_back  = x_spring + fp_t;
x_ns_front = x_ns_back - neg_slot_t;
W_neg      = x_ns_front - neg_skin;             // - wall inner face
x_nb       = x_ns_back + neg_back_t;            // - wall back face
X_len      = x_nb + fin_len;
Y_len      = n_cells * pitch;
function cy(i) = (i + 0.5) * pitch;

tower_w  = is_pogo ? tower_w_pogo : tower_w_plate;
force_dy = is_pogo ? 0 : -(sp_w + sp_gap)/2;
force_dz = is_pogo ? force_off : 0;
sense_dy = is_pogo ? 0 : (fp_w + sp_gap)/2;
sense_dz = is_pogo ? -sense_off : 0;

z_pogo   = z_cell - sense_off;
x_tip_p  = P + pos_pogo_preload;                 // + pogo tip (free)
x_bar_p  = x_tip_p - pogo_ext;                   // + pogo barrel front
x_tail_p = x_bar_p - (pogo_len - pogo_ext);      // + pogo tail end
x_head_stop_p = x_tip_p - pogo_work;             // + head bottoms here (max compression)
x_tip_n  = P + cell_len_min - neg_pogo_preload;  // - pogo tip (free)
x_bar_n  = x_tip_n + pogo_ext;
x_tail_n = x_bar_n + (pogo_len - pogo_ext);
x_head_stop_n = x_tip_n + pogo_work + 0.4;       // - head bottoms here

rib_x     = [for (f = rib_frac) P + f*cell_len_nom];
sw        = sqrt(r_s*r_s - rib_drop*rib_drop) + saddle_wing;
z_rib_top = z_axs - rib_drop;

fan_hs = fan_hole_spacing > 0 ? fan_hole_spacing :
         fan_size == 40 ? 32 : fan_size == 60 ? 50 : fan_size == 80 ? 71.5 :
         fan_size == 92 ? 82.5 : fan_size == 120 ? 105 : fan_size*0.89;

// gauge thicknesses (pogo tip protrusion in front of the wall faces)
gauge_pos = x_tip_p - W_pos;
gauge_neg = W_neg - x_tip_n;

// ------------------------------------------------------------- sanity checks
assert(r_s - cell_d_max/2 >= 0.3, "radial clearance < 0.3 mm");
assert(base_t >= min_wall && rail_t >= min_wall && race_lip_t >= min_wall, "wall < min_wall");
assert(X_len <= max_x && Y_len <= max_y, "cradle does not fit the bed");
assert(H + pin_h <= max_z, "cradle too tall");
assert(tunnel_w <= 10 && exit_w <= 10, "bridge > 10 mm");
assert(!is_pogo || x_tail_p > race_lip_t, "+ pogo tail hits raceway lip, increase race_w");

// ------------------------------------------- parameter export for the checker
function _jv(v) = is_string(v) ? str("\"", v, "\"")
                : is_list(v)   ? str("[", _jl(v, 0), "]") : str(v);
function _jl(l, i) = i >= len(l) ? "" : str(_jv(l[i]), i < len(l)-1 ? "," : "", _jl(l, i+1));
function _jo(kv, i = 0) = i >= len(kv) ? "" :
    str("\"", kv[i][0], "\":", _jv(kv[i][1]), i < len(kv)-1 ? "," : "", _jo(kv, i+1));

params = [
  ["part", part], ["sense_style", sense_style], ["n_cells", n_cells],
  ["bed_max", [max_x, max_y, max_z]],
  ["cell_d_nom", cell_d_nom], ["cell_d_max", cell_d_max],
  ["cell_len_min", cell_len_min], ["cell_len_nom", cell_len_nom], ["cell_len_max", cell_len_max],
  ["cap_d_min", cap_d_min], ["cap_d_max", cap_d_max], ["cap_h_min", cap_h_min],
  ["r_saddle", r_s], ["z_saddle_bottom", z_sb], ["z_saddle_axis", z_axs], ["z_cell", z_cell],
  ["H", H], ["pin_h", pin_h], ["pin_d", pin_d], ["stack_clr", stack_clr],
  ["pitch", pitch], ["cell_y", [for (i = [0:n_cells-1]) cy(i)]],
  ["X_len", X_len], ["Y_len", Y_len],
  ["P", P], ["W_pos", W_pos], ["x_pb", x_pb], ["W_neg", W_neg], ["x_nb", x_nb],
  ["x_spring", x_spring], ["L_contact", L_contact],
  ["fp_w", fp_w], ["fp_h", fp_h], ["fp_t", fp_t], ["fp_t_tol", fp_t_tol],
  ["pocket_d", pocket_d], ["cap_clear_d", cap_clear_d],
  ["force_dy", force_dy], ["force_dz", force_dz], ["sense_dy", sense_dy], ["sense_dz", sense_dz],
  ["sp_w", sp_w], ["sp_h", sp_h],
  ["spring_free_min", spring_free_min], ["spring_free_max", spring_free_max],
  ["spring_solid", spring_solid], ["spring_preload_min", spring_preload_min],
  ["spring_base_d", spring_base_d], ["neg_window_w", neg_window_w],
  ["pogo_hole_d", pogo_hole_d], ["pogo_head_d", 1.5], ["pogo_len", pogo_len], ["pogo_ext", pogo_ext],
  ["pogo_work", pogo_work], ["pogo_full", pogo_full],
  ["pos_pogo_preload", pos_pogo_preload], ["neg_pogo_preload", neg_pogo_preload],
  ["z_pogo", z_pogo], ["x_tip_p", x_tip_p], ["x_head_stop_p", x_head_stop_p], ["x_bar_p", x_bar_p], ["x_tail_p", x_tail_p],
  ["x_tip_n", x_tip_n], ["x_head_stop_n", x_head_stop_n], ["x_bar_n", x_bar_n], ["x_tail_n", x_tail_n],
  ["gauge_pos", gauge_pos], ["gauge_neg", gauge_neg],
  ["rib_x", rib_x], ["ntc_rib_index", ntc_rib_index], ["ntc_d", ntc_d], ["ntc_depth", ntc_depth],
  ["race_lip_t", race_lip_t], ["race_w", race_w], ["fin_len", fin_len],
  ["min_wall", min_wall], ["base_t", base_t], ["tunnel_w", tunnel_w], ["exit_w", exit_w],
  ["pcb_w", pcb_w], ["pcb_h", pcb_h], ["pcb_hole_inset", pcb_hole_inset],
  ["fan_size", fan_size], ["fan_hole_spacing", fan_hs]
];
echo(str("LFP_PARAMS_JSON=", "{", _jo(params), "}"));

// =================================================================== helpers
module box(p0, p1) { translate(p0) cube([p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]]); }

// cylinder along X from x0 to x1; holes are circumscribed so they print true size
module xcyl(x0, x1, y, z, d, fn = fn_small) {
  translate([min(x0, x1), y, z]) rotate([0, 90, 0])
    cylinder(r = d/2/cos(180/fn), h = abs(x1 - x0), $fn = fn);
}
// cylinder along Y
module ycyl(y0, y1, x, z, d, fn = fn_small) {
  translate([x, min(y0, y1), z]) rotate([-90, 0, 0])
    cylinder(r = d/2/cos(180/fn), h = abs(y1 - y0), $fn = fn);
}

// place 2-D children on a face: o = centre, u = glyph right, v = glyph up,
// n = outward normal (u x v must equal n, so text is never mirrored)
module on_face(o, u, v, n) {
  multmatrix([[u[0], v[0], n[0], o[0]],
              [u[1], v[1], n[1], o[1]],
              [u[2], v[2], n[2], o[2]],
              [0, 0, 0, 1]]) children();
}
module deboss(o, u, v, n, depth = label_depth) {
  on_face(o, u, v, n) translate([0, 0, -depth]) linear_extrude(depth + 0.5) children();
}

// 7-segment glyphs (font independent, >= 0.9 mm strokes for a 0.4 mm nozzle)
SEG7 = [[1,1,1,1,1,1,0], [0,1,1,0,0,0,0], [1,1,0,1,1,0,1], [1,1,1,1,0,0,1],
        [0,1,1,0,0,1,1], [1,0,1,1,0,1,1], [1,0,1,1,1,1,1], [1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1], [1,1,1,1,0,1,1]];
module digit2d(d, h) {
  w = 0.6*h; t = max(0.16*h, 0.9);
  r = [[[-w/2, h/2 - t], [w/2, h/2]],  [[w/2 - t, 0], [w/2, h/2]],
       [[w/2 - t, -h/2], [w/2, 0]],    [[-w/2, -h/2], [w/2, -h/2 + t]],
       [[-w/2, -h/2], [-w/2 + t, 0]],  [[-w/2, 0], [-w/2 + t, h/2]],
       [[-w/2, -t/2], [w/2, t/2]]];
  for (k = [0:6]) if (SEG7[d][k] == 1) translate(r[k][0]) square(r[k][1] - r[k][0]);
}
module number2d(n, h) {               // 1 or 2 digits
  if (n < 10) digit2d(n, h);
  else { translate([-0.4*h, 0]) digit2d(floor(n/10), h); translate([0.4*h, 0]) digit2d(n % 10, h); }
}
module plus2d(s)  { t = max(0.22*s, 1.0); square([s, t], center = true); square([t, s], center = true); }
module minus2d(s) { t = max(0.22*s, 1.0); square([s, t], center = true); }
// circle with a 45 deg pointed roof (+Y = up after placement), roof truncated
// so that the remaining flat bridge is at most max_bridge wide
module teardrop2d(r, max_bridge = 10) {
  intersection() {
    hull() { circle(r = r, $fn = 96); translate([0, r*sqrt(2)]) square(0.01, center = true); }
    translate([-2*r, -2*r]) square([4*r, 2*r + r*sqrt(2) - max_bridge/2]);
  }
}

// ================================================================== CRADLE
module cradle(lab = label_offset) {
  union() {
    difference() {
      cradle_solids();
      cradle_cuts(lab);
    }
    // stacking pins (added after the cuts)
    for (p = post_centres()) translate([p[0], p[1], H - 0.01]) {
      cylinder(d = pin_d, h = pin_h - 1 + 0.01, $fn = 48);
      translate([0, 0, pin_h - 1]) cylinder(d1 = pin_d, d2 = pin_d - 2, h = 1, $fn = 48);
    }
  }
}

function post_centres() = [[post/2, post/2], [post/2, Y_len - post/2],
                           [X_len - post/2, post/2], [X_len - post/2, Y_len - post/2]];

module cradle_solids() {
  // ---- floor: solid under walls/raceway, strips under the cells, rest open
  box([0, 0, 0], [W_pos, Y_len, base_t]);
  box([W_neg, 0, 0], [x_nb, Y_len, base_t]);
  for (i = [0:n_cells-1]) {
    box([W_pos - 1, cy(i) - strip_w/2, 0], [W_neg + 1, cy(i) + strip_w/2, base_t]);
    box([x_nb - 1, cy(i) - fin_t/2 - 3, 0], [X_len, cy(i) + fin_t/2 + 3, base_t]);
  }
  box([X_len - 4, 0, 0], [X_len, Y_len, base_t]);
  // ---- side rails
  for (y0 = [0, Y_len - rail_t]) box([0, y0, 0], [X_len, y0 + rail_t, rail_h]);
  // ---- raceway (outer lip + closed ends)
  box([0, 0, 0], [race_lip_t, Y_len, race_lip_h]);
  for (y0 = [0, Y_len - rail_t]) box([0, y0, 0], [x_pb + 0.01, y0 + rail_t, race_lip_h]);
  // ---- end walls: low web + one contact tower per cell
  box([x_pb, 0, 0], [x_pb + web_t, Y_len, web_h]);
  box([x_nb - web_t, 0, 0], [x_nb, Y_len, web_h]);
  for (i = [0:n_cells-1]) {
    box([x_pb, cy(i) - tower_w/2, 0], [W_pos, cy(i) + tower_w/2, H]);
    box([W_neg, cy(i) - tower_w/2, 0], [x_nb, cy(i) + tower_w/2, H]);
    // fins behind both walls: pogo support (pogo style) + zip-tie anchors (always)
    box([x_pb - fin_len, cy(i) - fin_t/2, 0], [x_pb + 0.01, cy(i) + fin_t/2, z_pogo + fin_above]);
    box([x_nb - 0.01, cy(i) - fin_t/2, 0], [X_len, cy(i) + fin_t/2, z_pogo + fin_above]);
  }
  // ---- saddle ribs
  for (k = [0:len(rib_x)-1]) rib(k);
  // ---- corner posts (carry the stacking pins / sockets)
  for (p = post_centres()) translate([p[0] - post/2, p[1] - post/2, 0]) cube([post, post, H]);
}

module rib(k) {
  xc = rib_x[k];
  t  = (k == ntc_rib_index) ? ntc_rib_t : rib_t;
  difference() {
    union() {
      for (i = [0:n_cells-1]) box([xc - t/2, cy(i) - sw, 0], [xc + t/2, cy(i) + sw, z_rib_top]);
      box([xc - t/2, 0, 0], [xc + t/2, Y_len, rib_link_h]);
    }
    for (i = [0:n_cells-1]) {
      c = cy(i);
      translate([xc - t/2 - 1, c, z_axs]) rotate([0, 90, 0])
        cylinder(r = r_s / cos(180/fn_saddle), h = t + 2, $fn = fn_saddle);
      for (s = [-1, 1]) {
        box([xc - t/2 - 1, c + s*wire_notch_off - tunnel_w/2, base_t],
            [xc + t/2 + 1, c + s*wire_notch_off + tunnel_w/2, base_t + tunnel_h]);
        translate([xc, c + s*sw, z_rib_top]) rotate([45, 0, 0])
          cube([t + 2, ch_rib*sqrt(2), ch_rib*sqrt(2)], center = true);
      }
    }
  }
}

module cradle_cuts(lab) {
  for (i = [0:n_cells-1]) {
    pos_tower_cuts(i);
    neg_tower_cuts(i);
    // NTC pocket + lead groove in the middle rib (global cut: also cuts the floor strip)
    xn = rib_x[ntc_rib_index];
    translate([xn, cy(i), z_sb - ntc_depth]) cylinder(r = ntc_d/2/cos(180/32), h = r_s + ntc_depth, $fn = 32);
    box([xn - ntc_lead_w/2, cy(i) - wire_notch_off, z_sb - ntc_depth],
        [xn + ntc_lead_w/2, cy(i), z_axs]);
    // wire exit U-notch in the raceway lip
    hull() {
      xcyl(-1, race_lip_t + 1, cy(i), exit_floor + exit_w/2, exit_w, 48);
      box([-1, cy(i) - exit_w/2, exit_floor + exit_w/2], [race_lip_t + 1, cy(i) + exit_w/2, race_lip_h + 1]);
    }
    // zip-tie slots through the fins (strain relief for the 6-wire bundle)
    for (xs = [x_pb - 4, x_pb - 11, x_nb + 4, x_nb + 11])
      box([xs - tie_w/2, cy(i) - fin_t/2 - 1, 6], [xs + tie_w/2, cy(i) + fin_t/2 + 1, 6 + tie_h]);
    cell_labels(i, i + 1 + lab);
  }
  // ventilation slots along the floor strips in the middle bays (end bays stay
  // solid: they carry the big polarity marks)
  for (i = [0:n_cells-1], k = [0:len(rib_x)-2]) {
    t0 = (k == ntc_rib_index) ? ntc_rib_t : rib_t;
    t1 = (k + 1 == ntc_rib_index) ? ntc_rib_t : rib_t;
    hull() for (x = [rib_x[k] + t0/2 + 4 + strip_slot_w/2, rib_x[k+1] - t1/2 - 4 - strip_slot_w/2])
      translate([x, cy(i), -1]) cylinder(d = strip_slot_w, h = base_t + 2, $fn = 32);
  }
  // raceway floor openings between the cell bays (wires stay in their own bay)
  for (k = [0:n_cells-2]) {
    y0 = cy(k) + tower_w/2 + 3; y1 = cy(k+1) - tower_w/2 - 3;
    if (y1 - y0 > 6) hull() for (x = [race_lip_t + 6, x_pb - 6], y = [y0 + 3, y1 - 3])
      translate([x, y, -1]) cylinder(r = 3, h = base_t + 2, $fn = 24);
  }
  // stacking sockets (bottom of the posts): 45 deg cone roof, no bridge
  for (p = post_centres()) translate([p[0], p[1], 0]) {
    ds = pin_d + 2*stack_clr;
    translate([0, 0, -0.01]) cylinder(d = ds, h = pin_h + 0.51, $fn = 48);
    translate([0, 0, pin_h + 0.5]) cylinder(d1 = ds, d2 = 0, h = ds/2, $fn = 48);
    translate([0, 0, -0.01]) cylinder(d1 = ds + 1.2, d2 = ds, h = 0.6, $fn = 48);
  }
}

module pos_plate_cut(yc, zc, w, h, nw) {
  // pocket, open to the top: plate slides in from the top, tab pointing UP
  box([W_pos - pocket_d, yc - w/2 - fp_clr, zc - h/2 - 0.5], [W_pos + 1, yc + w/2 + fp_clr, H + 1]);
  // tab notch through the back: pre-bend the tab 90 deg backwards, slide the plate
  // down - the tab rides down the notch and the plate hangs on it at the right
  // height; solder the wire to the tab end behind the wall
  box([x_pb - 1, yc - nw/2, zc + h/2 + pos_notch_drop], [W_pos - pocket_d + 0.01, yc + nw/2, H + 1]);
}

module neg_slot_cut(yc, zc, w, h, ww, sbd, nw) {
  // captured slot (plate edges held behind the 1.2 mm skin), open to the top
  box([x_ns_front, yc - w/2 - fp_clr, zc - h/2 - 0.2], [x_ns_back, yc + w/2 + fp_clr, H + 1]);
  // window in the skin for the conical spring (open to the top so it slides in)
  box([W_neg - 1, yc - ww/2, zc - sbd/2 - 0.5], [x_ns_front + 0.01, yc + ww/2, H + 1]);
  // tab notch through the back
  box([x_ns_back - 0.01, yc - nw/2, zc + h/2 + neg_notch_drop], [x_nb + 1, yc + nw/2, H + 1]);
}

module pos_tower_cuts(i) {
  c = cy(i);
  translate([W_pos, c, H]) rotate([0, 45, 0]) cube([ch_top*sqrt(2), tower_w + 2, ch_top*sqrt(2)], center = true);
  // recess around the + cap so ONLY the plate (and the pogo) can touch it
  translate([W_pos - pocket_d, c, z_cell]) rotate([0, 90, 0]) cylinder(d = cap_clear_d, h = pocket_d + 1, $fn = 96);
  pos_plate_cut(c + force_dy, z_cell + force_dz, fp_w, fp_h, tab_notch_w);
  if (is_pogo) {
    // head travel: only as deep as the working stroke -> the step is a hard stop
    xcyl(x_head_stop_p, W_pos - pocket_d + 0.01, c, z_pogo, pogo_head_clear_d);
    xcyl(x_pb - 0.01, x_head_stop_p + 0.01, c, z_pogo, pogo_hole_d);             // barrel bore
    xcyl(x_pb - fin_len - 1, x_pb + 0.01, c, z_pogo, pogo_guide_d);               // fin guide
  } else {
    pos_plate_cut(c + sense_dy, z_cell + sense_dz, sp_w, sp_h, sp_notch_w);
  }
  for (s = [-1, 1])   // wire tunnels (B-F, B-S, NTC come from under the cell)
    box([x_pb - 1, c + s*wire_notch_off - tunnel_w/2, base_t], [W_pos + 1, c + s*wire_notch_off + tunnel_w/2, base_t + tunnel_h]);
}

module neg_tower_cuts(i) {
  c = cy(i);
  translate([W_neg, c, H]) rotate([0, 45, 0]) cube([ch_top*sqrt(2), tower_w + 2, ch_top*sqrt(2)], center = true);
  neg_slot_cut(c + force_dy, z_cell + force_dz, fp_w, fp_h, neg_window_w, spring_base_d, tab_notch_w);
  if (is_pogo) {
    xcyl(W_neg - 1, max(W_neg + 0.5, x_head_stop_n), c, z_pogo, pogo_head_clear_d);  // head travel / entry
    xcyl(max(W_neg + 0.5, x_head_stop_n) - 0.01, x_nb + 0.01, c, z_pogo, pogo_hole_d);  // barrel bore
    xcyl(x_nb - 0.01, X_len + 1, c, z_pogo, pogo_guide_d);         // fin guide
  } else {
    neg_slot_cut(c + sense_dy, z_cell + sense_dz, sp_w, sp_h, sp_window_w, sp_spring_base_d, sp_notch_w);
  }
  for (s = [-1, 1])
    box([W_neg - 1, c + s*wire_notch_off - tunnel_w/2, base_t], [x_nb + 1, c + s*wire_notch_off + tunnel_w/2, base_t + tunnel_h]);
}

// polarity marks and channel numbers (all debossed, label_depth deep)
module cell_labels(i, ch) {
  c  = cy(i);
  ex = [1, 0, 0]; ey = [0, 1, 0]; ez = [0, 0, 1];
  mx = [-1, 0, 0]; my = [0, -1, 0];
  lo = (tab_notch_w/2 + tower_w/2)/2;          // label centre offset on tower faces
  // raceway lip outer face (viewed from the connector side): "+  [exit]  N"
  deboss([0, c + 12, exit_floor + lip_digit_h/2 - 1], my, ez, mx) plus2d(lip_digit_h*0.8);
  deboss([0, c - 12, exit_floor + lip_digit_h/2 - 1], my, ez, mx) number2d(ch, lip_digit_h);
  // + tower back face (inside the raceway) and - tower back face
  for (s = [-1, 1]) deboss([x_pb, c + s*lo, 28], my, ez, mx) plus2d(7);
  deboss([x_nb, c - lo, 28], ey, ez, ex) minus2d(7);
  deboss([x_nb, c + lo, 28], ey, ez, ex) number2d(ch, 8);
  // inner (cell-facing) faces of the towers, both sides of the contact
  for (s = [-1, 1]) {
    deboss([W_pos, c + s*(tower_w/2 - 2.6), H - ch_top - 3.4], ey, ez, ex) plus2d(4.5);
    deboss([W_neg, c + s*(tower_w/2 - 2.6), H - ch_top - 3.4], my, ez, mx) minus2d(4.5);
  }
  // big marks on the floor strip right in front of each contact (seen when loading)
  xf_p = (W_pos + rib_x[0] - rib_t/2)/2;
  xf_n = (W_neg + rib_x[len(rib_x)-1] + rib_t/2)/2;
  deboss([xf_p, c, base_t], my, ex, ez) plus2d(min(12, strip_w - 6));
  deboss([xf_n, c, base_t], my, ex, ez) minus2d(min(12, strip_w - 6));
  if (is_pogo) {
    // tower tops: polarity sign + channel number (read from the + end)
    xt = x_pb + (pos_wall_t - ch_top)/2;
    deboss([xt, c + lo, H], my, ex, ez) plus2d(7);
    deboss([xt, c - lo, H], my, ex, ez) number2d(ch, min(top_digit_h, pos_wall_t - ch_top - 1.5));
    xn = (x_ns_back + x_nb)/2;
    deboss([xn, c - lo, H], ey, mx, ez) minus2d(7);
    deboss([xn, c + lo, H], ey, mx, ez) number2d(ch, min(top_digit_h, (x_nb - x_ns_back) - 1));
  }
}

// ============================================================= BOARD STAND
// PCB stands vertically (component side +Y), fan lies flat under the PCB and
// blows upwards along both faces of the board. Print as modelled (base down).
// The PCB stiffens the frame once screwed on; an X-brace keeps it square.
module board_stand() {
  hx      = pcb_w/2 - pcb_hole_inset;
  z_pcb0  = stand_base_t + fan_gap + fan_t + 6;          // PCB bottom edge
  zh      = [z_pcb0 + pcb_hole_inset, z_pcb0 + pcb_h - pcb_hole_inset];
  up_top  = z_pcb0 + pcb_h + 2;
  yf      = -standoff_h;                                 // upright front face
  yb      = -standoff_h - upright_d;                     // upright back face
  y_fan   = pcb_t/2;                                     // fan centred on PCB plane
  bx      = hx + upright_w/2 + 4;
  by0     = min(yb - 24, y_fan - fan_size/2 - 6);
  by1     = y_fan + fan_size/2 + 6;
  difference() {
    union() {
      // base frame: plate with a fan intake opening and two large side windows
      difference() {
        box([-bx, by0, 0], [bx, by1, stand_base_t]);
        translate([fan_dx, y_fan, -1]) cylinder(d = fan_hs - 10, h = stand_base_t + 2, $fn = 96);
        for (s = [-1, 1]) {
          wx0 = fan_size/2 + 8; wx1 = hx - upright_w/2 - 6;
          if (wx1 - wx0 > 10) hull() for (x = [wx0 + 5, wx1 - 5], y = [by0 + 13, by1 - 13])
            translate([s*x, y, -1]) cylinder(r = 5, h = stand_base_t + 2, $fn = 32);
        }
      }
      for (s = [-1, 1]) {
        // upright
        box([s*hx - upright_w/2, yb, 0], [s*hx + upright_w/2, yf, up_top]);
        // gussets front / back
        hull() {
          box([s*hx - 2, yf - 0.01, 0], [s*hx + 2, yf, 40]);
          box([s*hx - 2, by1 - 2, 0], [s*hx + 2, by1, stand_base_t]);
        }
        hull() {
          box([s*hx - 2, yb, 0], [s*hx + 2, yb + 0.01, 70]);
          box([s*hx - 2, by0, 0], [s*hx + 2, by0 + 2, stand_base_t]);
        }
        // PCB standoffs with 45 deg gusset underneath (no support needed)
        for (z = zh) hull() {
          translate([s*hx, yf - 0.01, z]) rotate([-90, 0, 0]) cylinder(d = standoff_d, h = standoff_h + 0.01, $fn = 32);
          box([s*hx - standoff_d/2, yf - 0.01, z - standoff_d/2 - standoff_h], [s*hx + standoff_d/2, yf, z - standoff_d/2 - standoff_h + 0.01]);
        }
      }
      // X brace in the back plane (bars steeper than 45 deg)
      for (s = [-1, 1]) hull() {
        box([s*hx - 3, yb, 0], [s*hx + 3, yb + 3, 6]);
        box([-s*hx - 3, yb, up_top - 6], [-s*hx + 3, yb + 3, up_top]);
      }
      // fan posts
      for (sx = [-1, 1], sy = [-1, 1])
        translate([fan_dx + sx*fan_hs/2, y_fan + sy*fan_hs/2, 0]) cylinder(d = 8, h = stand_base_t + fan_gap, $fn = 32);
    }
    // M3 pilot holes for the PCB
    for (s = [-1, 1], z = zh) ycyl(yf - 12, 0.5, s*hx, z, screw_pilot_d);
    // fan screw pilots
    for (sx = [-1, 1], sy = [-1, 1])
      translate([fan_dx + sx*fan_hs/2, y_fan + sy*fan_hs/2, 1.2]) cylinder(r = screw_pilot_d/2/cos(180/fn_small), h = stand_base_t + fan_gap, $fn = fn_small);
    // screw-down holes in the base corners
    for (sx = [-1, 1], yy = [by0 + 6, by1 - 6])
      translate([sx*(bx - 6), yy, -1]) cylinder(d = 4.5, h = stand_base_t + 2, $fn = 24);
  }
}

// ========================================================= BOARD TRAY (acc.)
// Sits on top of a stacked module (uses the cradle pins). PCB lies flat with
// its long side along Y; a fan wall at +X blows across the board.
module board_tray() {
  hx = pcb_w/2 - pcb_hole_inset;   // along Y
  hy = pcb_h/2 - pcb_hole_inset;   // along X
  pcx = 6 + pcb_h/2 + 2;           // PCB centre (X)
  pcy = Y_len/2;
  fx  = pcx + pcb_h/2 + 6;         // fan wall inner face
  fz  = tray_t + 2;                // fan bottom
  tray_roof_h = (fan_size - 4)/2*sqrt(2) - 5;   // top of the truncated teardrop
  difference() {
    union() {
      // frame
      difference() {
        box([0, 0, 0], [X_len, Y_len, tray_t]);
        for (sy = [-1, 1]) translate([pcx, pcy + sy*Y_len/4, -1])
          linear_extrude(tray_t + 2) offset(r = 6) square([pcb_h - 40, Y_len/2 - 30], center = true);
        translate([(fx + X_len)/2 + 2, pcy, -1]) linear_extrude(tray_t + 2) offset(r = 4) square([X_len - fx - 30, Y_len - 50], center = true);
      }
      for (p = post_centres()) translate([p[0] - post/2, p[1] - post/2, 0]) cube([post, post, pin_h + 3]);
      // standoffs
      for (sx = [-1, 1], sy = [-1, 1]) translate([pcx + sx*hy, pcy + sy*hx, 0]) cylinder(d = standoff_d, h = tray_t + standoff_h, $fn = 32);
      // fan wall
      box([fx, pcy - fan_size/2 - 6, 0], [fx + 4, pcy + fan_size/2 + 6, fz + fan_size/2 + tray_roof_h + 3]);
      for (sy = [-1, 1]) hull() {     // buttresses
        box([fx + 4, pcy + sy*(fan_size/2 + 4) - 2, 0], [fx + 4.01, pcy + sy*(fan_size/2 + 4) + 2, fz + fan_size]);
        box([fx + 4, pcy + sy*(fan_size/2 + 4) - 2, 0], [min(X_len, fx + 30), pcy + sy*(fan_size/2 + 4) + 2, tray_t]);
      }
    }
    for (p = post_centres()) translate([p[0], p[1], 0]) {
      ds = pin_d + 2*stack_clr;
      translate([0, 0, -0.01]) cylinder(d = ds, h = pin_h + 0.51, $fn = 48);
      translate([0, 0, pin_h + 0.5]) cylinder(d1 = ds, d2 = 0, h = ds/2, $fn = 48);
    }
    for (sx = [-1, 1], sy = [-1, 1]) translate([pcx + sx*hy, pcy + sy*hx, 1.2]) cylinder(r = screw_pilot_d/2/cos(180/fn_small), h = 30, $fn = fn_small);
    // fan opening: circle with a 45 deg roof truncated to a 10 mm bridge
    translate([fx - 1, pcy, fz + fan_size/2]) rotate([90, 0, 90]) linear_extrude(6)
      teardrop2d((fan_size - 4)/2, 10);
    for (sy = [-1, 1], sz = [-1, 1])
      xcyl(fx - 1, fx + 6, pcy + sy*fan_hs/2, fz + fan_size/2 + sz*fan_hs/2, 3.4);
  }
}

// ========================================================== POGO GAUGE (acc.)
// Lay the flat side on the wall face, slide the slot around the pogo plunger:
// the free tip must be flush with the top of the matching pad.
module pogo_gauge() {
  pad = 22; slot = 2.6;
  difference() {
    union() {
      box([0, 0, 0], [pad, pad, gauge_pos]);
      box([pad, 4, 0], [pad + 26, pad - 4, 3]);
      box([pad + 26, 0, 0], [2*pad + 26, pad, gauge_neg]);
    }
    // slots open towards -Y
    box([pad/2 - slot/2, -1, -1], [pad/2 + slot/2, pad/2, 10]);
    box([pad + 26 + pad/2 - slot/2, -1, -1], [pad + 26 + pad/2 + slot/2, pad/2, 10]);
    deboss([pad + 8, pad/2, 3], [1, 0, 0], [0, 1, 0], [0, 0, 1], 0.6) plus2d(7);
    deboss([pad + 18, pad/2, 3], [1, 0, 0], [0, 1, 0], [0, 0, 1], 0.6) minus2d(7);
  }
}

// ================================================================ ASSEMBLY
module dummy_cell(i, z0) {
  zc = z0 + z_sb + cell_d_nom/2;
  color("SteelBlue") translate([P + 0.8, cy(i), zc]) rotate([0, 90, 0]) cylinder(d = cell_d_nom, h = cell_len_nom - 0.8, $fn = 64);
  color("Silver") translate([P, cy(i), zc]) rotate([0, 90, 0]) cylinder(d = 15, h = 0.81, $fn = 48);
}
module assembly() {
  color("Orange") render() cradle(0);          // render(): keeps the preview CSG tree small
  color("DarkOrange") translate([0, 0, H]) render() cradle(4);
  for (i = [0:n_cells-1]) { dummy_cell(i, 0); dummy_cell(i, H); contact_hardware(i, 0); contact_hardware(i, H); }
  // board stand next to the connector (+) end, PCB parallel to the end face,
  // component side towards the cells
  translate([-62, Y_len/2, 0]) rotate([0, 0, -90]) {
    color("LightGray") render() board_stand();
    color("ForestGreen") translate([-pcb_w/2, 0, stand_base_t + fan_gap + fan_t + 6]) cube([pcb_w, pcb_t, pcb_h]);
    color("DimGray") translate([fan_dx - fan_size/2, pcb_t/2 - fan_size/2, stand_base_t + fan_gap]) cube([fan_size, fan_size, fan_t]);
  }
}

// contact hardware as installed with a nominal cell (visual only)
module contact_hardware(i, z0 = 0, L = cell_len_nom) {
  c  = cy(i);
  zc = z0 + z_sb + cell_d_nom/2;              // resting cell axis
  zf = z0 + z_cell + force_dz;
  // + force plate with tab bent back over the notch floor
  color("Gainsboro") {
    translate([P - fp_t, c + force_dy - fp_w/2, zf - fp_h/2]) cube([fp_t, fp_w, fp_h]);
    translate([P - fp_t - 6, c + force_dy - 2, zf + fp_h/2]) cube([6 + fp_t, 4, fp_t]);
  }
  // - spring plate + conical spring compressed to the cell end
  color("Gainsboro") {
    translate([x_spring, c + force_dy - fp_w/2, zf - fp_h/2]) cube([fp_t, fp_w, fp_h]);
    translate([x_spring, c + force_dy - 2, zf + fp_h/2]) cube([6, 4, fp_t]);
  }
  color("Silver") translate([P + L, c + force_dy, zf]) rotate([0, 90, 0])
    difference() {
      cylinder(d1 = 5, d2 = spring_base_d, h = x_spring - P - L, $fn = 32);
      translate([0, 0, -0.01]) cylinder(d1 = 4, d2 = spring_base_d - 1, h = x_spring - P - L + 0.02, $fn = 32);
    }
  if (is_pogo) {
    zp = z0 + z_pogo;
    color("Gold") {
      // + pogo, compressed by pos_pogo_preload (tip on the cap = plane P)
      xcyl(x_tail_p, x_bar_p, c, zp, 1.36, 16);
      xcyl(x_bar_p, P - 0.3, c, zp, 0.99, 16);
      xcyl(P - 0.3, P, c, zp, 1.5, 16);
      // - pogo, tip on the cell end
      xcyl(x_bar_n, x_tail_n, c, zp, 1.36, 16);
      xcyl(P + L + 0.3, x_bar_n, c, zp, 0.99, 16);
      xcyl(P + L, P + L + 0.3, c, zp, 1.5, 16);
    }
  }
  // NTC bead on foam in the middle-rib pocket
  color("Black") translate([rib_x[ntc_rib_index], c, z0 + z_sb - 1.4]) sphere(d = 2.8, $fn = 24);
  color("Khaki") translate([rib_x[ntc_rib_index], c, z0 + z_sb - ntc_depth]) cylinder(d = ntc_d - 0.4, h = ntc_depth - 2.8, $fn = 24);
}

// cut through cell 1 along its axis, + end on the left when seen from -Y;
// contact hardware drawn whole, the cell as a transparent ghost
module section_view() {
  i = 1;
  difference() {
    cradle(0);
    translate([-60, -1, -1]) cube([X_len + 120, cy(i) + 1, H + pin_h + 2]);
  }
  contact_hardware(i);
  %dummy_cell(i, 0);
}

// ================================================================ dispatch
if (part == "cradle")           cradle(label_offset);
else if (part == "board_stand") board_stand();
else if (part == "board_tray")  board_tray();
else if (part == "pogo_gauge")  pogo_gauge();
else if (part == "assembly")    assembly();
else if (part == "section")     section_view();
