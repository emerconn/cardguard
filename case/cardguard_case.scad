// cardguard case v1: front shell + friction-fit back lid for the ESP32-2424S012N-I.
// All board numbers come from docs/hardware.md (caliper measurements).
//
// Coordinates (mm): looking at the BACK of the board from +z: +x right, +y away from the USB tab (tab at -y),
// origin = PCB circle centre. Same x as the "back view" in docs/hardware.md; y is flipped (the doc's +y
// points towards the tab).
// z = 0 is the screen glass front; the board's back faces +z.
//
// Render one part:  openscad -D 'part="shell"' -o shell.stl cardguard_case.scad   (or "lid")
// Preview:          part="assembly" (board ghost + cut-away case)

part = "assembly";  // "shell" | "lid" | "board" | "assembly" | "section" (2D slice at x = section_x)
section_x = 0;
cutaway = true;     // assembly: cut the case in half to show the board inside

/* [Board, measured] */
pcb_d = 37.0;           // PCB circle
tab_w = 14.23;          // USB tab width
tab_end = 20.0;         // PCB circle centre to the tab's end edge (38.5 overall - 18.5)
tab_len = 3.74;         // tab end edge to where its angled sides meet the circle (datasheet drawing)
fpc_out = 0.5;          // display flex cable wrap past the tab end
glass_d = 36.7;         // screen glass
view_d = 32.4;          // visible display area
z_pcb_front = 2.0;      // glass front to PCB front
z_pcb_back = 3.2;       // glass front to PCB back
z_tallest = 7.7;        // glass front to the tallest back part (battery JST)

btn_x = 12.0;           // REST at -x, BOOT at +x (back view)
btn_y = 9.9;
plunger = [2.2, 3.0];   // oval plunger, x by y
z_btn_top = z_pcb_back + 2.5;

usb_w = 8.8;            // shell width; sits on the PCB back, flush with the tab end
usb_x = tab_w / 2 - usb_w / 2;  // not centred: flush with the tab's +x side edge (vendor render)
usb_h = 3.0;
module_xy = [0, 11.5];  // ESP32-C3 module (13.2 x 12.5, metal can), centre estimated from the render
z_module_top = z_pcb_back + 2.4;
z_usb_top = z_pcb_back + usb_h;

sw_y = -0.3;            // power switch lever centre (19.7 from the tab end), on the +x edge
sw_len = 2.0;           // lever size along the edge
sw_housing = [2.0, 4.6, 2.5];  // switch body: depth into the board (est.), length along the edge, height (est.)

// Battery connector: JST 1.25 mm 2-pin, 90 degree SMT (PicoBlade-style; sizes from the CJT A1251WR-S-2P
// clone datasheet: 7.75 overall with solder tabs, body 4.70 deep, 3.20 tall). Angle from the vendor render;
// centre placed so the farthest corner is 15.5 from the tab end (measured).
bat_len = 7.75;         // overall along the pin row, including the metal solder tabs
bat_body = [5.75, 4.7]; // plastic body: along the pin row (overall minus the ~1 mm tabs) x depth
bat_angle = 45;         // pin row direction, from +x
bat_corner = (bat_body[0] + bat_body[1]) / 2 * sin(45);  // centre to farthest corner, along y
bat_xy = [11.8, -(tab_end - 15.5) - bat_corner];
z_bat_top = z_tallest;  // measured total says 4.5 above the PCB back; the datasheet part is 3.2 (to check)
sw_out = 0.7;           // lever sticks out past the PCB edge
z_sw = [z_pcb_back + 1.5, z_pcb_back + 2.5];

/* [Case] */
clear = 0.25;           // board to pocket wall, each side
wall = 1.6;             // side wall
lip = 1.2;              // front lip thickness, in front of the glass
view_open = 33.4;       // front opening: visible area + 0.5 per side, hides a <=0.5 glass offset
back_gap = 0.3;         // tallest part to the lid's inner face
lid_t = 2.0;
step_w = 0.8;           // stepped rim: ledge cut into the shell wall's top edge, the lid's flange rests on it
step_h = 0.8;           // (holds the lid up all round, so pressing near an edge can't tip it)
fit = 0.05;             // lid edge to shell wall, each side (0.1 printed loose; raise if too tight to push in)
press_gap = 0.2;        // lid posts to the module / USB tops (0 = touching; 0.1 rocked slightly on the posts)
btn_hole_extra = 0.6;   // button hole over the plunger, total
plug = [11, 6.6];       // USB opening, sized for a plug overmold so most cables seat fully;
                        // wider cuts through the case corner, since the port is off-centre
plug_r = 1.5;
pry_w = 6;              // fingernail notch on the back rim for opening the lid

$fn = 128;

z_lid = z_tallest + back_gap;   // lid inner face
z_top = z_lid + lid_t;          // shell back rim = lid outer face
usb_z = (z_pcb_back + z_usb_top) / 2;

// ---------------------------------------------------------------- 2D outlines

// The USB tab is a trapezoid: tab_w wide at its end, sides flaring out (~24 degrees) until they meet the
// circle tab_len further in. fpc adds room past the end for the display flex cable (pocket only: the
// PCB itself ends flush with the USB shell).
module board_outline(extra = 0, fpc = 0) {
  y_meet = -(tab_end - tab_len);
  x_meet = sqrt(pow(pcb_d / 2, 2) - pow(y_meet, 2));
  offset(delta = extra) {
    circle(d = pcb_d);
    polygon([[-tab_w / 2, -(tab_end + fpc)], [tab_w / 2, -(tab_end + fpc)], [tab_w / 2, -tab_end],
             [x_meet, y_meet], [x_meet, -10], [-x_meet, -10], [-x_meet, y_meet], [-tab_w / 2, -tab_end]]);
  }
}
module pocket_outline() { board_outline(clear, fpc_out); }
module outer_outline() { offset(r = wall) pocket_outline(); }
module oval(size) { scale([size[0], size[1]]) circle(d = 1, $fn = 48); }
module rounded_rect(size, r) { offset(r = r) square([size[0] - 2 * r, size[1] - 2 * r], center = true); }

// ---------------------------------------------------------------- parts

module shell() {
  difference() {
    translate([0, 0, -lip]) linear_extrude(z_top + lip) outer_outline();
    // board pocket, open at the back
    linear_extrude(z_top + 1) pocket_outline();
    // ledge for the lid's flange
    translate([0, 0, z_top - step_h]) linear_extrude(step_h + 1) offset(r = step_w) pocket_outline();
    // front opening with a 45 degree chamfer on the outside
    translate([0, 0, -lip - 0.01]) cylinder(h = lip + 0.02, d1 = view_open + 2 * lip, d2 = view_open);
    // USB opening through the tab end wall
    translate([usb_x, -(tab_end + fpc_out + clear + wall / 2), usb_z]) rotate([90, 0, 0])
      linear_extrude(wall * 2 + 1, center = true) rounded_rect(plug, plug_r);
    // relief for the power switch lever, open towards the back so the board drops in
    translate([pcb_d / 2 - 0.5, sw_y - sw_len / 2 - 0.5, z_sw[0] - 0.4])
      cube([0.5 + sw_out + 0.4, sw_len + 1, z_top]);
    // fingernail notch on the back rim, opposite the USB tab
    translate([-pry_w / 2, pcb_d / 2 + clear - 0.5, z_top - 1]) cube([pry_w, wall + 1, 2]);
  }
}

module lid() {
  difference() {
    union() {
      translate([0, 0, z_lid]) linear_extrude(lid_t) offset(delta = -fit) pocket_outline();
      translate([0, 0, z_top - step_h]) linear_extrude(step_h) offset(delta = -fit) offset(r = step_w) pocket_outline();
      // posts press the board forward onto the front lip: on the module can and the USB shell
      translate([module_xy[0], module_xy[1], z_module_top + press_gap]) cylinder(h = z_lid - z_module_top - press_gap + 0.01, d = 4);
      translate([usb_x - 3, -tab_end + 1, z_usb_top + press_gap]) cube([6, 3, z_lid - z_usb_top - press_gap + 0.01]);
    }
    for (s = [-1, 1])
      translate([s * btn_x, btn_y, z_lid - 1]) linear_extrude(lid_t + 2) oval(plunger + [1, 1] * btn_hole_extra);
  }
}

// ---------------------------------------------------------------- board ghost (preview only)

module board() {
  color("#222") cylinder(h = z_pcb_front, d = glass_d);
  color("#3a6") translate([0, 0, z_pcb_front]) linear_extrude(z_pcb_back - z_pcb_front) board_outline();
  // display flex cable wrapping round the tab end, screen side only (width along the edge is a guess)
  color("orange") translate([-5, -(tab_end + fpc_out), 0.5]) cube([10, fpc_out, z_pcb_back - 0.5]);
  color("silver") {
    translate([module_xy[0] - 6.6, module_xy[1] - 6.25, z_pcb_back]) cube([13.2, 12.5, 2.4]);
    translate([usb_x - usb_w / 2, -tab_end, z_pcb_back]) cube([usb_w, 7.3, usb_h]);
  }
  for (s = [-1, 1]) {
    color("#ccc") translate([s * btn_x - 1.7, btn_y - 2.1, z_pcb_back]) cube([3.45, 4.2, 1.5]);
    color("#444") translate([s * btn_x, btn_y, z_pcb_back]) linear_extrude(2.5) oval(plunger);
  }
  // battery connector: body, solder tabs at both ends, plug opening facing the board centre and open on
  // top (seen on our board), so a battery lead leaves towards the middle of the board; the 2 pins are on
  // the same, centre-facing side (vendor render, confirmed)
  translate([bat_xy[0], bat_xy[1], z_pcb_back]) rotate(bat_angle) {
    difference() {
      color("#ccd") translate([-bat_body[0] / 2, -bat_body[1] / 2, 0]) cube([bat_body[0], bat_body[1], z_bat_top - z_pcb_back]);
      color("#555") translate([-bat_body[0] / 2 + 0.5, bat_body[1] / 2 - 2.2, 0.6]) cube([bat_body[0] - 1, 2.21, z_bat_top - z_pcb_back]);
    }
    for (e = [-1, 1]) color("gold") translate([e * (bat_len / 2 - 0.5) - 0.5, -1.5, 0]) cube([1, 3, 0.3]);
    for (p = [-0.625, 0.625]) color("gold") translate([p - 0.4, bat_body[1] / 2, 0]) cube([0.8, 1.2, 0.3]);
  }
  // power switch: body on the PCB back at the +x edge, lever sticking out past the edge
  color("#ddd") translate([pcb_d / 2 - sw_housing[0], sw_y - sw_housing[1] / 2, z_pcb_back]) cube(sw_housing);
  color("orange") translate([pcb_d / 2 - 0.5, sw_y - sw_len / 2, z_sw[0]]) cube([0.5 + sw_out, sw_len, 1]);
}

// Slice through the plane x = section_x, laid flat: horizontal = y, vertical = z (screen front at the bottom).
module section(x) {
  projection(cut = true) rotate([0, -90, 0]) translate([-x, 0, 0]) children();
}

if (part == "shell") shell();
else if (part == "lid") translate([0, 0, z_top]) rotate([180, 0, 0]) lid();  // print orientation: outer face on the bed, posts up (rotated, never mirror)
else if (part == "board") board();
else if (part == "section") {
  color("#8ab") section(section_x) shell();
  color("#c96") section(section_x) lid();
  color("#3a6") section(section_x) board();
}
else {
  board();
  difference() {  // cut away the -x half so the fit is visible
    union() { color("#8ab", 0.6) shell(); color("#c96", 0.8) lid(); }
    if (cutaway) translate([-50, -50, -10]) cube([50, 100, 30]);
  }
}
