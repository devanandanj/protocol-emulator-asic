# SPDX-License-Identifier: Apache-2.0
#
# Adapted from 2AMLogic/sg13cmos5l-protocol-emulator src/pdn_cfg.tcl
# (Apache-2.0, https://github.com/2AMLogic/sg13cmos5l-protocol-emulator,
# PR #64), which first got an RM_IHPSG13 SRAM macro through the Tiny
# Tapeout CMOS5L flow. Their header follows unchanged. For PEmu the macro
# instance is core.u_sram, placed at the same x (91.2 um) as theirs, so the
# strap-lattice alignment (FP_PDN_VPITCH 44.96 / FP_PDN_VOFFSET 93.985)
# carries over; only the y position differs for the 6x4 die.
#
# Custom PDN configuration for the SRAM-macro-backed top (issue #60).
#
# This file replaces LibreLane's default pdn_cfg.tcl wholesale via
# FP_PDN_CFG = dir::pdn_cfg.tcl in src/config.json (LibreLane 3.1.0.dev3's
# OpenROAD.GeneratePDN reads it as PDN_CFG).
#
# WHY a custom file: the default ends with an unconditional macro grid whose
# add_pdn_connect wants a via stack between PDN_VERTICAL_LAYER (Metal4, the
# macro's only power-pin layer) and PDN_HORIZONTAL_LAYER (TopMetal1).  With
# FP_PDN_MULTILAYER=0 there are no TopMetal1 straps to attach to, so pdngen
# fails PDN-0232/PDN-0233, and TopMetal1 straps are not an option anyway:
# TopMetal1.drawing is not on the competition precheck's CMOS5L layer
# allowlist (only TopMetal1.nofill is), so a user project may not draw power
# straps there.
#
# The RM_IHPSG13_1P_256x16_c2_bm_bist's VDD!/VSS!/VDDARRAY! pins are
# Metal4-only vertical stripes (2.81 um wide, 11.24 um same-net pitch) and
# every other x position over the macro is Metal4-obstructed, so the ONLY
# legal Metal4 over the macro is a shape that coincides with one of the
# macro's own stripes.  OpenROAD's pdngen cannot place such shapes itself:
#   - a core-grid strap at a stripe x is severed by the net-less LEF-OBS
#     halo of the stripes that flank it (verified on the step-19 ODB of run
#     36773701780: zero straps survive over the macro, at any width), and
#     same-net stripes only exempt a strap that fully contains them, which
#     the flanking obstructions make geometrically impossible; and
#   - a macro-grid strap is claimed away entirely by the stdcell grid's
#     domain obstruction (the core domain outline unites the macro back in,
#     so the core grid claims Metal4 over the macro for every other grid;
#     verified: define_pdn_grid -macro + add_pdn_stripe yields "does not
#     contain any shapes or vias").
# This file therefore draws the bridge itself: one fixed special-net Metal4
# STRIPE per macro power stripe, on that pin's globally-connected net, before
# pdngen runs.  pdngen's makeInitialShapes() harvests existing special-net
# boxes as fixed shapes, and the stdcell grid's Metal1<->Metal4 connect then
# places the via stacks from those stripes down to the standard-cell rails,
# wiring the macro's stripes into the core grid on the same layer, with no
# layer above Metal4 anywhere.
#
# Bridge extents are edge-directed (not full core height): the macro's array
# regions carry a Metal4 obstruction band across the stripe x positions
# between VDD!'s top edge and VDDARRAY!'s bottom edge (LEF OBS y 39.085 to
# 45.205 local), so VDD! (bottom-anchored) is bridged down to the core
# bottom, VDDARRAY! (top-anchored) up to the core top, and only the
# full-height stripes (VSS! everywhere, the four full-height periphery VDD!
# stripes) get full-core-height bridges.  Verified locally on the step-19 ODB
# of run 36773701780: check_power_grid reports "All shapes on net VPWR/VGND
# are connected" and the DEF geometry audit shows zero opposite-net overlaps
# and zero gap-band intrusions.
#
# The core strap lattice is re-pitched by src/config.json from the template's
# FP_PDN_VPITCH 50.0 / (PDK default) FP_PDN_VOFFSET 10.0 to 44.96 / 93.985
# so that every core strap crossing the macro lands either exactly on a
# same-net macro stripe (merging with the bridge) or in clear space between
# stripes.  The template's 50.0 um pitch is NOT safe next to full-height
# bridges: its strap pair at x=112.88/116.98 um would overlap the macro
# stripes' bridge at x=112.32/117.94 um with opposite nets -- 0.09 um of
# direct short on both pairs.

proc pdn_bridge_draw {} {
    if { ![info exists ::env(PDN_MACRO_CONNECTIONS)] } {
        puts stderr "pdn_bridge: PDN_MACRO_CONNECTIONS is not set"
        exit 1
    }
    set block [ord::get_db_block]
    set tech [$block getTech]
    set m4 [$tech findLayer Metal4]
    if {$m4 eq "NULL"} {
        puts stderr "pdn_bridge: Metal4 not found in tech"
        exit 1
    }
    set core [$block getCoreArea]
    set y0 [$core yMin]
    set y1 [$core yMax]

    set drawn 0
    foreach conn $::env(PDN_MACRO_CONNECTIONS) {
        set iname [lindex $conn 0]
        set inst [$block findInst $iname]
        if {$inst eq "NULL"} {
            puts stderr "pdn_bridge: instance '$iname' not found"
            exit 1
        }
        set irect [$inst getBBox]
        set iy0 [$irect yMin]
        set iy1 [$irect yMax]
        # De-duplicate identical bridge rectangles: VDD! and VDDARRAY! share
        # x positions but have different y extents, so key on all four edges.
        array set seen {}
        foreach iterm [$inst getITerms] {
            set mterm [$iterm getMTerm]
            set sig [$mterm getSigType]
            if {$sig ne "POWER" && $sig ne "GROUND"} {
                continue
            }
            set net [$iterm getNet]
            if {$net eq "NULL"} {
                puts stderr "pdn_bridge: pin [$mterm getName] of $iname has no net (global connect missing?)"
                exit 1
            }
            foreach geom [$iterm getGeometries] {
                lassign $geom glayer grect
                if {[$glayer getName] ne "Metal4"} {
                    puts "pdn_bridge: skipping non-Metal4 supply pin geometry of [$mterm getName] on layer [$glayer getName]"
                    continue
                }
                set x0 [$grect xMin]
                set x1 [$grect xMax]
                set gy0 [$grect yMin]
                set gy1 [$grect yMax]
                # Which macro edges does this stripe reach? (1 dbu tolerance)
                set at_bottom [expr {abs($gy0 - $iy0) <= 1}]
                set at_top [expr {abs($gy1 - $iy1) <= 1}]
                if {$at_bottom && $at_top} {
                    set by0 $y0
                    set by1 $y1
                } elseif {$at_bottom} {
                    set by0 $y0
                    set by1 $gy1
                } elseif {$at_top} {
                    set by0 $gy0
                    set by1 $y1
                } else {
                    puts stderr "pdn_bridge: [$mterm getName] stripe at x=$x0 does not reach either macro edge -- no legal Metal4 bridge exists"
                    exit 1
                }
                set key "$x0,$x1,$by0,$by1"
                if {[info exists seen($key)]} {
                    continue
                }
                set seen($key) 1
                set swire [odb::dbSWire_create $net ROUTED]
                odb::dbSBox_create $swire $m4 $x0 $by0 $x1 $by1 STRIPE
                incr drawn
            }
        }
    }
    puts "\[INFO\] pdn_bridge: drew $drawn Metal4 bridge stripes over $iname"
}

pdn_bridge_draw

# ---------------------------------------------------------------------------
# Standard-cell grid -- identical to LibreLane's default pdn_cfg.tcl
# (PDN_MULTILAYER=0 path uses only the vertical layer), except that no macro
# grid is defined: the bridge above replaces it.  (The default's io.tcl
# helpers are already sourced by pdn.tcl before this file runs.)
# ---------------------------------------------------------------------------
source $::env(SCRIPTS_DIR)/openroad/common/set_global_connections.tcl
set_global_connections

set secondary []
foreach vdd $::env(VDD_NETS) gnd $::env(GND_NETS) {
    if { $vdd != $::env(VDD_NET)} {
        lappend secondary $vdd

        set db_net [[ord::get_db_block] findNet $vdd]
        if {$db_net == "NULL"} {
            set net [odb::dbNet_create [ord::get_db_block] $vdd]
            $net setSpecial
            $net setSigType "POWER"
        }
    }

    if { $gnd != $::env(GND_NET)} {
        lappend secondary $gnd

        set db_net [[ord::get_db_block] findNet $gnd]
        if {$db_net == "NULL"} {
            set net [odb::dbNet_create [ord::get_db_block] $gnd]
            $net setSpecial
            $net setSigType "GROUND"
        }
    }
}

set_voltage_domain -name CORE -power $::env(VDD_NET) -ground $::env(GND_NET) \
    -secondary_power $secondary

if { $::env(PDN_MULTILAYER) == 1 } {
    error "pdn_cfg: PDN_MULTILAYER=1 is not supported for this design (TopMetal1.drawing is not on the CMOS5L precheck allowlist)"
}

set arg_list [list]

define_pdn_grid \
    -name stdcell_grid \
    -starts_with POWER \
    -voltage_domain CORE \
    {*}$arg_list

set arg_list [list]
if { [info exists ::env(PDN_EXTEND_TO)] } {
    if { $::env(PDN_EXTEND_TO) eq "core_ring" } {
        lappend arg_list -extend_to_core_ring
    } elseif { $::env(PDN_EXTEND_TO) eq "boundary" } {
        lappend arg_list -extend_to_boundary
    }
}

add_pdn_stripe \
    -grid stdcell_grid \
    -layer $::env(PDN_VERTICAL_LAYER) \
    -width $::env(PDN_VWIDTH) \
    -pitch $::env(PDN_VPITCH) \
    -offset $::env(PDN_VOFFSET) \
    -spacing $::env(PDN_VSPACING) \
    -starts_with POWER \
    {*}$arg_list

if { $::env(PDN_ENABLE_RAILS) == 1 } {
    add_pdn_stripe \
        -grid stdcell_grid \
        -layer $::env(PDN_RAIL_LAYER) \
        -width $::env(PDN_RAIL_WIDTH) \
        -followpins

    add_pdn_connect \
        -grid stdcell_grid \
        -layers "$::env(PDN_RAIL_LAYER) $::env(PDN_VERTICAL_LAYER)"
}

# ---------------------------------------------------------------------------
# Power pins (replaces pin promotion on the stdcell grid).
#
# The Tiny Tapeout precheck (tt-support-tools precheck/pin_check.py,
# power-pins loop) requires EVERY VPWR/VGND port rect in the streamed LEF to
# be within 10 um of BOTH die edges -- i.e. every power pin must be a
# full-die-height strap.  Promoting the stdcell grid's straps (-pins on that
# grid, the default behavior) fails that with a macro in the die: straps
# crossing the macro's x-range are split by its halo into partial rects, and
# a separate pins-only grid cannot work either, because the stdcell grid
# claims the whole core domain on its layers for every other grid.  So the
# two pin shapes are created directly, on the same strap lattice positions
# the stdcell grid itself uses to the RIGHT of the macro (whose x-range is
# [91.2, 328.0] on this die), where the underlying straps run uninterrupted
# from die edge to die edge.  The pin boxes coincide with those straps, so
# this adds no new metal -- only the block pins the flow and the precheck
# need.
# ---------------------------------------------------------------------------
proc pdn_pins_create {} {
    set block [ord::get_db_block]
    set tech [$block getTech]
    set m4 [$tech findLayer Metal4]
    if {$m4 eq "NULL"} {
        puts stderr "pdn_pins: Metal4 not found in tech"
        exit 1
    }
    set dbu [$block getDbUnitsPerMicron]
    set die [$block getDieArea]
    set y0 [$die yMin]
    set y1 [$die yMax]
    # Strap centers on the stdcell grid's lattice (offset is measured from
    # the core corner to the first power-strap center; ground follows by
    # width + spacing).  Seven pitch periods from the offset lands right of
    # the macro's x-range on this die (366.625 / 370.725 um for the values
    # in src/config.json).
    set core [$block getCoreArea]
    set cx_vdd [expr {([$core xMin] + ($::env(PDN_VOFFSET) + 6 * $::env(PDN_VPITCH)) * $dbu)}]
    set cx_vss [expr {$cx_vdd + ($::env(PDN_VWIDTH) + $::env(PDN_VSPACING)) * $dbu}]
    set halfw [expr {int($::env(PDN_VWIDTH) * $dbu / 2)}]
    foreach pair [list [list $::env(VDD_NET) $cx_vdd] [list $::env(GND_NET) $cx_vss]] {
        lassign $pair net_name cx
        set net [$block findNet $net_name]
        if {$net eq "NULL"} {
            puts stderr "pdn_pins: net '$net_name' not found"
            exit 1
        }
        set bterm [$net get1stBTerm]
        if {$bterm eq "NULL"} {
            set bterm [odb::dbBTerm_create $net $net_name]
            $bterm setIoType INOUT
            $bterm setSpecial
            # The DisconnectedPins checker classifies block terminals by
            # their own sig type, not the net's -- set it from the net.
            $bterm setSigType [$net getSigType]
        }
        set bpin [odb::dbBPin_create $bterm]
        $bpin setPlacementStatus FIRM
        odb::dbBox_create $bpin $m4 [expr {int($cx) - $halfw}] $y0 [expr {int($cx) + $halfw}] $y1
        puts "\[INFO\] pdn_pins: created $net_name pin at x=[expr {$cx / $dbu}] um spanning the die height"
    }
}

if { $::env(PDN_ENABLE_PINS) } {
    pdn_pins_create
}

if { $::env(PDN_CORE_RING) == 1 } {
    error "pdn_cfg: PDN_CORE_RING=1 is not supported by this custom configuration"
}
