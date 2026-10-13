// Akashic Display sensor node: a ceiling-mounted case in two parts.
//
//   part = "shell"  the box, open at the top (the ceiling side), floor-facing face down
//   part = "lid"    the plate that screws to the ceiling and closes the box
//   part = "both"   both, side by side, to look at
//
// Three compartments, so the readings aren't spoiled:
//   1. Electronics: the board and its network/power cable. Vented at the ceiling end, so
//      its heat leaves away from the sensors.
//   2. Air: the PMS5003 (its fan's intake and outlet on side-wall slots, kept apart so it
//      doesn't breathe its own air) and the SCD41.
//   3. Climate "chimney": the SHT40 alone, behind a double wall with an air gap, open to
//      the room below and at the side. A sensor in the same box as a warm board reads
//      several degrees high.
// The floor-facing face has an open window for each VL53L5CX: never cover these with
// clear plastic or glass, which blocks or reflects the sensor's infrared light. And a hole
// for the TSL2591's light.
//
// Print in PETG or ASA, not PLA: the sensors and board stay warm day and night, and a PLA
// case near a ceiling can sag over months. Untested: no case has been printed yet. Check
// every board's size against yours (marked "measure") before printing.
//
// Render:  openscad -D 'part="shell"' -o shell.stl sensor-node.scad

part = "both";
// Two doorway sensors (1) or one (0).
two_tof = 1;

/* [Walls] */
wall = 2.4;
floor_t = 2.4;
gap = 3;          // the air gap between the electronics and the climate chimney
corner = 4;

/* [Parts (mm)] */
// Plantower PMS5003 (datasheet: 50 x 38 x 21).
pms = [50, 38, 21];
// The board: Olimex ESP32-POE-ISO or an ESP32-S3-DevKitC-1 (measure yours).
board = [80, 30, 20];
// SparkFun VL53L5CX Qwiic breakout, 1" square (measure yours).
tof_board = 25.4;
// The window over each VL53L5CX (it sees 45 degrees each way; keep the window wide).
tof_window = 14;
// Adafruit SHT40 / SCD41 / TSL2591 breakouts are about 25.4 x 17.8 (measure yours).
small = [25.4, 17.8];

/* [Box (outside, mm)] */
inner_h = 34;
air_w = pms[0] + 8;           // the air bay's width
chimney_w = small[0] + 6;     // the climate chimney's width
box_w = wall + board[0] + 6 + wall + air_w + wall + gap + wall + chimney_w + wall;
box_d = wall + max(board[1] + 12, pms[1] + small[1] + 14, 2 * tof_board + 10) + wall;
box_h = floor_t + inner_h;

/* [Mounting] */
screw_d = 4.5;     // ceiling screws
insert_d = 4.2;    // M3 heat-set inserts in the shell's corner posts
lid_t = 3;

$fn = 48;

// Where each compartment starts (x), inside the walls.
x_elec = wall;
x_air = x_elec + board[0] + 6 + wall;
x_gap = x_air + air_w;
x_chimney = x_gap + wall + gap + wall;

module rounded_box(size, r) {
  hull() for (x = [r, size[0] - r], y = [r, size[1] - r]) translate([x, y, 0]) cylinder(r = r, h = size[2]);
}

module slots(n, len, w, pitch) {
  for (i = [0 : n - 1]) translate([i * pitch, 0, 0]) cube([w, len, 50], center = false);
}

module shell() {
  difference() {
    rounded_box([box_w, box_d, box_h], corner);
    // Hollow out the three compartments (open at the top, the ceiling side).
    translate([x_elec, wall, floor_t]) cube([board[0] + 6, box_d - 2 * wall, inner_h + 1]);
    translate([x_air, wall, floor_t]) cube([air_w, box_d - 2 * wall, inner_h + 1]);
    translate([x_chimney, wall, floor_t]) cube([chimney_w, box_d - 2 * wall, inner_h + 1]);
    // The air gap between the air bay's wall and the chimney's: open at both ends.
    translate([x_gap + wall, -1, floor_t]) cube([gap, box_d + 2, inner_h + 1]);

    // Floor face: the time-of-flight windows (under the electronics bay's front half)...
    for (i = [0 : two_tof]) {
      translate([x_elec + 12 + i * (tof_board + 8), wall + 6, -1]) cube([tof_window, tof_window, floor_t + 2]);
    }
    // ...the light sensor's hole, under the air bay...
    translate([x_air + air_w - 12, box_d / 2, -1]) cylinder(d = 5, h = floor_t + 2);
    // ...and the chimney's floor slots, open to the room.
    translate([x_chimney + 3, wall + 4, -1]) slots(5, box_d - 2 * wall - 8, 2, (chimney_w - 6) / 4.5);

    // Air bay: the PMS5003's intake and outlet slots, on opposite sides of its fan face.
    translate([x_air + 6, -1, floor_t + 4]) rotate([0, 0, 0]) cube([14, wall + 2, pms[2] - 6]);
    translate([x_air + air_w - 20, -1, floor_t + 4]) cube([14, wall + 2, pms[2] - 6]);
    // The SCD41's own vents on the back wall.
    for (i = [0 : 3]) translate([x_air + 8 + i * 6, box_d - wall - 1, floor_t + 6]) cube([3, wall + 2, 16]);

    // Chimney: side slots, so room air flows up through it.
    for (i = [0 : 3]) translate([box_w - wall - 1, wall + 6 + i * ((box_d - 2 * wall - 16) / 3), floor_t + 4]) cube([wall + 2, 3, inner_h - 10]);

    // Electronics bay: the cable (RJ45 or USB-C) at the end wall, and vents high up (the
    // ceiling end) for its heat.
    translate([-1, box_d / 2 - 9, floor_t + 6]) cube([wall + 2, 18, 15]);
    for (i = [0 : 5]) translate([x_elec + 10 + i * 10, -1, box_h - 8]) cube([4, wall + 2, 5]);
    for (i = [0 : 5]) translate([x_elec + 10 + i * 10, box_d - wall - 1, box_h - 8]) cube([4, wall + 2, 5]);

    // Inserts for the lid screws.
    for (p = posts()) translate([p[0], p[1], box_h - 8]) cylinder(d = insert_d, h = 9);
  }
  // Corner posts for the lid's screws.
  for (p = posts()) translate([p[0], p[1], floor_t]) difference() {
    cylinder(d = 8, h = inner_h);
    translate([0, 0, inner_h - 8]) cylinder(d = insert_d, h = 9);
  }
  // Stand-offs: the PMS5003 rests on rails, clear of the floor.
  translate([x_air + 4, wall + 2, floor_t]) cube([pms[0], 3, 3]);
  translate([x_air + 4, wall + pms[1] - 1, floor_t]) cube([pms[0], 3, 3]);
}

function posts() = [
  [wall + 5, wall + 5], [box_w - wall - 5, wall + 5],
  [wall + 5, box_d - wall - 5], [box_w - wall - 5, box_d - wall - 5],
];

module lid() {
  difference() {
    rounded_box([box_w, box_d, lid_t], corner);
    // Screws into the shell's inserts.
    for (p = posts()) translate([p[0], p[1], -1]) cylinder(d = 3.4, h = lid_t + 2);
    // Two keyhole-free ceiling screw holes, countersunk from the box side.
    for (x = [box_w * 0.3, box_w * 0.7]) translate([x, box_d / 2, -1]) {
      cylinder(d = screw_d, h = lid_t + 2);
      translate([0, 0, lid_t - 1.5]) cylinder(d1 = screw_d, d2 = screw_d + 4, h = 1.6);
    }
    // A cable hole, for a cable from above the ceiling.
    translate([wall + 15, box_d / 2, -1]) cylinder(d = 10, h = lid_t + 2);
    // The air gap stays open at the ceiling end too.
    translate([x_gap + wall, wall, -1]) cube([gap, box_d - 2 * wall, lid_t + 2]);
  }
}

if (part == "shell") shell();
else if (part == "lid") lid();
else {
  shell();
  translate([0, box_d + 10, 0]) lid();
}
echo(str("Case outside: ", box_w, " x ", box_d, " x ", box_h + lid_t, " mm"));
