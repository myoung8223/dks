// Digispark Case w/GPIO Access
// By: Mike Young - May 8, 2026 - Version 8
// Website: https://www.mikesshorts.com/
// YouTube: https://www.youtube.com/@user-sf5dn3mo4e

/*
Copyright 2026 Mike Young

This work is licensed under the Creative Commons Attribution 4.0 International License (CC BY 4.0).

You are free to:
 * Share
 * Remix
 * Adapt
 * Use commercially

Under the following terms:
 * Attribution required

No warranty is provided; use at your own risk.

License: https://creativecommons.org/licenses/by/4.0/
Legal code: https://creativecommons.org/licenses/by/4.0/legalcode
*/

$fn = 100;

rndr = 0;   // 0: render bottom, 1: render top

pcbw = 19.01+0.1;   // pcb width
pcbl = 17.73+0.1;   // pcb length
pcbt = 4.5+0.1;     // pcb thickness (including thickest component)
csr = 1;          // corner sphere radius
cwt = 2.75;    // case wall thickness, was 2
sh = 2;   // switch height
bt = 1.6;        // button thickness
bd = 3.47+0.1;                 // button diameter
ct = cwt + pcbt + sh + bt/2;   // case thickness
swl = 6.18+0.3;                // switch width/length
pcbgx = 20;       // pcb grip x, was 6
pcbgy = 2.92;   // pcb grip y
pcbgz = 3.5;      // pcb grip z
pcbat = 2.325;      // pcb actual thickness
shdd = 4.25+0.1;   // screw head diameter
shdt = 0.88;       // screw head thickness
sts = 1.75;    // screw torus size
sto = 2.25;    // screw torus offset, was 1.5
lhd = 2;         // LED hole diameter
shdtop = 2.38;   // screw hole diameter for top
shdbtm = 2.18;   // screw hole diameter for bottom
dpcd = 2.83;     // DuPont connector dimension, was 2.63, increased for tolerance


difference() {

    union() {
        
        hull() {
        //union() {   // union for visualizing corner spheres

            translate([pcbw/2 - csr + cwt, pcbl/2 - csr, csr])
            sphere(r=csr);

            translate([-pcbw/2 + csr - cwt, pcbl/2 - csr, csr])
            sphere(r=csr);

            translate([pcbw/2 - csr + cwt, -pcbl/2 + csr - cwt, csr])
            sphere(r=csr);

            translate([-pcbw/2 + csr - cwt, -pcbl/2 + csr - cwt, csr])
            sphere(r=csr);


            translate([pcbw/2 - csr + cwt, pcbl/2 - csr, -csr + ct])
            sphere(r=csr);

            translate([-pcbw/2 + csr - cwt, pcbl/2 - csr, -csr + ct])
            sphere(r=csr);

            translate([pcbw/2 - csr + cwt, -pcbl/2 + csr - cwt, -csr + ct])
            sphere(r=csr);

            translate([-pcbw/2 + csr - cwt, -pcbl/2 + csr - cwt, -csr + ct])
            sphere(r=csr);

        }

        hull() {
            
            translate([pcbw/2+sto, 0, ct-csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);

            translate([pcbw/2+sto, 0, csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);
        }

        hull() {
            
            translate([-pcbw/2-sto, 0, ct-csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);

            translate([-pcbw/2-sto, 0, csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);
        }

        hull() {
            
            translate([0, -pcbw/2-sto/2, ct-csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);

            translate([0, -pcbw/2-sto/2, csr])
            rotate_extrude(convexity = 10, $fn = 100)
                translate([sts, 0, 0])
                    circle(r = csr);
            
        }

    }

    union() {

        //translate([swl/2+0.75, -swl/2-0.75, 5])
        //cylinder(d=lhd, h=20);

        if (rndr == 0) {
            translate([pcbw/2+sto, 0, 1])
            cylinder(d=shdbtm, h=20);

            translate([-pcbw/2-sto, 0, 1])
            cylinder(d=shdbtm, h=20);

            translate([0, -pcbw/2-sto/2, 1])
            cylinder(d=shdbtm, h=20);
        }
        else {
            translate([pcbw/2+sto, 0, 1])
            cylinder(d=shdtop, h=20);

            translate([-pcbw/2-sto, 0, 1])
            cylinder(d=shdtop, h=20);

            translate([0, -pcbw/2-sto/2, 1])
            cylinder(d=shdtop, h=20);
        }


        translate([pcbw/2+sto, 0, ct - shdt])
        cylinder(d=shdd, h=20);

        translate([-pcbw/2-sto, 0, ct - shdt])
        cylinder(d=shdd, h=20);

        translate([0, -pcbw/2-sto/2, ct - shdt])
        cylinder(d=shdd, h=20);

//!union() {
        // pcb cut-out
        translate([0, 0, pcbt/2 + cwt])
        cube([pcbw, pcbl, pcbt], center=true);

        // elevated pcb footprint cut-out for tactbutton legs
        translate([0, 0, pcbt/2 + cwt+1])      
        cube([pcbw, pcbl, pcbt], center=true);
        
        // insert cut-outs for jumpers here
        hull() {
        translate([-pcbw/2+1.52, pcbl/2-2.5, 11.5])
        cube([dpcd, dpcd, 20], center=true);
        translate([-pcbw/2+1.52, pcbl/2-2.5-5.13, 11.5])
        cube([dpcd, dpcd, 20], center=true);
        }
        
        hull() {
        translate([pcbw/2-2, -pcbl/2+2, 11.5])
        cube([dpcd, dpcd, 20], center=true);
        translate([pcbw/2-2-12.57, -pcbl/2+2, 11.5])
        cube([dpcd, dpcd, 20], center=true);
        }
        
//}

        //translate([0, 0, sh/2 + cwt + pcbt])
        //cube([swl, swl, sh], center=true);

        //translate([0, 0, 5])
        //cylinder(d=bd, h=8);

        if (rndr == 0) {                   // if rndr is 0 then render bottom
            translate([0, 0, 15 + ct/2])
            cube([30, 30, 30], center=true);
        }

        if (rndr == 1) {                        // if rndr is 1 then render top
            translate([0, 0, 15 + ct/2 - 30])
            cube([30, 30, 30], center=true);
        }

    }

}

if (rndr == 1) {
    difference() {
        // pcb grip piece, gets added to the top piece
        translate([0, -pcbgy/2 + pcbl/2, pcbgz/2 + cwt + pcbat])
        cube([pcbgx, pcbgy, pcbgz], center=true);

        // insert cut-outs for jumpers here
        hull() {
            translate([-pcbw/2+1.52, pcbl/2-2.5, 13])
            cube([dpcd, dpcd, 20], center=true);
            translate([-pcbw/2+1.52, pcbl/2-2.5-5.13, 13])
            cube([dpcd, dpcd, 20], center=true);
        }
    }
}
