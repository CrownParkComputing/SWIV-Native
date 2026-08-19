/* table.c -- behaviour dispatch (generated from re/handlers.txt: gfx word -> handler) */
#include "../engine/engine.h"
#include <string.h>

void bh_foddera_2(Obj *o) __attribute__((weak));   /* FODDERA.LIN#2 @ 0x213d68 */
void bh_medtank_0(Obj *o) __attribute__((weak));   /* MEDTANK.LIN#0 @ 0x215bea */
void bh_popup_0(Obj *o) __attribute__((weak));   /* POPUP.LIN#0 @ 0x2163ce */
void bh_rotobase_12(Obj *o) __attribute__((weak));   /* ROTOBASE.LIN#12 @ 0x21566e */
void bh_mine_0(Obj *o) __attribute__((weak));   /* MINE.LIN#0 @ 0x215836 */
void bh_flame_0(Obj *o) __attribute__((weak));   /* FLAME.LIN#0 @ 0x216830 */
void bh_proxmine_0(Obj *o) __attribute__((weak));   /* PROXMINE.LIN#0 @ 0x216700 */
void bh_train_0(Obj *o) __attribute__((weak));   /* TRAIN.LIN#0 @ 0x21589e */
void bh_camogun_0(Obj *o) __attribute__((weak));   /* CAMOGUN.LIN#0 @ 0x216932 */
void bh_yellow_0(Obj *o) __attribute__((weak));   /* YELLOW.LIN#0 @ 0x2143dc */
void bh_bird_0(Obj *o) __attribute__((weak));   /* BIRD.LIN#0 @ 0x21434e */
void bh_jets_0(Obj *o) __attribute__((weak));   /* JETS.LIN#0 @ 0x2159c0 */
void bh_jets_1(Obj *o) __attribute__((weak));   /* JETS.LIN#1 @ 0x215a3e */
void bh_blackjet_0(Obj *o) __attribute__((weak));   /* BLACKJET.LIN#0 @ 0x2137b8 */
void bh__onerig_0(Obj *o) __attribute__((weak));   /* _ONERIG.LIN#0 @ 0x213e86 */
void bh_pyramid_1(Obj *o) __attribute__((weak));   /* PYRAMID.LIN#1 @ 0x216586 */
void bh_eggs_2(Obj *o) __attribute__((weak));   /* EGGS.LIN#2 @ 0x214198 */
void bh_diagun_2(Obj *o) __attribute__((weak));   /* DIAGUN.LIN#2 @ 0x21648e */
void bh_diagun_4(Obj *o) __attribute__((weak));   /* DIAGUN.LIN#4 @ 0x2164a8 */
void bh_destrain_3(Obj *o) __attribute__((weak));   /* DESTRAIN.LIN#3 @ 0x215ed0 */
void bh_trilo_4(Obj *o) __attribute__((weak));   /* TRILO.LIN#4 @ 0x213f8a */
void bh__plat_9(Obj *o) __attribute__((weak));   /* _PLAT.LIN#9 @ 0x2160d2 */
void bh__plat_10(Obj *o) __attribute__((weak));   /* _PLAT.LIN#10 @ 0x2160d8 */
void bh_truck_0(Obj *o) __attribute__((weak));   /* TRUCK.LIN#0 @ 0x215a84 */
void bh_vtol_0(Obj *o) __attribute__((weak));   /* VTOL.LIN#0 @ 0x214064 */
void bh__airport_14(Obj *o) __attribute__((weak));   /* _AIRPORT.LIN#14 @ 0x213690 */
void bh__corn_7(Obj *o) __attribute__((weak));   /* _CORN.LIN#7 @ 0x213f2c */
void bh_jeepheli_43(Obj *o) __attribute__((weak));   /* JEEPHELI.LIN#43 @ 0x216a50 */
void bh_mama_0(Obj *o) __attribute__((weak));   /* MAMA.LIN#0 @ 0x2138ca */
void bh_flattank_0(Obj *o) __attribute__((weak));   /* FLATTANK.LIN#0 @ 0x215b24 */
void bh_tilt_0(Obj *o) __attribute__((weak));   /* TILT.LIN#0 @ 0x213b08 */
void bh_fish_0(Obj *o) __attribute__((weak));   /* FISH.LIN#0 @ 0x216ec8 */
void bh__rigs_4(Obj *o) __attribute__((weak));   /* _RIGS.LIN#4 @ 0x216f4a */
void bh_juntank_2(Obj *o) __attribute__((weak));   /* JUNTANK.LIN#2 @ 0x2162b2 */
void bh_juntank_1(Obj *o) __attribute__((weak));   /* JUNTANK.LIN#1 @ 0x215df2 */
void bh_lakegun_0(Obj *o) __attribute__((weak));   /* LAKEGUN.LIN#0 @ 0x216f8c */
void bh_lakegun_7(Obj *o) __attribute__((weak));   /* LAKEGUN.LIN#7 @ 0x216ffc */
void bh_lakesub_0(Obj *o) __attribute__((weak));   /* LAKESUB.LIN#0 @ 0x21706a */
void bh_hover_4(Obj *o) __attribute__((weak));   /* HOVER.LIN#4 @ 0x217186 */
void bh_xevious_5(Obj *o) __attribute__((weak));   /* XEVIOUS.LIN#5 @ 0x21363a */
void bh_xevious_9(Obj *o) __attribute__((weak));   /* XEVIOUS.LIN#9 @ 0x213bf8 */
void bh_xevious_0(Obj *o) __attribute__((weak));   /* XEVIOUS.LIN#0 @ 0x216b12 */
void bh_seaplane_0(Obj *o) __attribute__((weak));   /* SEAPLANE.LIN#0 @ 0x2172bc */
void bh_edge_0(Obj *o) __attribute__((weak));   /* EDGE.LIN#0 @ 0x2134e6 */
void bh_skyeyea_0(Obj *o) __attribute__((weak));   /* SKYEYEA.LIN#0 @ 0x213318 */
void bh_skyeyeb_8(Obj *o) __attribute__((weak));   /* SKYEYEB.LIN#8 @ 0x21340c */
void bh_tinytruk_0(Obj *o) __attribute__((weak));   /* TINYTRUK.LIN#0 @ 0x216bfc */
void bh_airmine_0(Obj *o) __attribute__((weak));   /* AIRMINE.LIN#0 @ 0x2132c8 */
void bh_ski_0(Obj *o) __attribute__((weak));   /* SKI.LIN#0 @ 0x215b80 */
void bh_tap_0(Obj *o) __attribute__((weak));   /* TAP.LIN#0 @ 0x215700 */
void bh_eggs_12(Obj *o) __attribute__((weak));   /* EGGS.LIN#12 @ 0x216604 */
void bh__lava_20(Obj *o) __attribute__((weak));   /* _LAVA.LIN#20 @ 0x216cbc */
void bh_bunny_2(Obj *o) __attribute__((weak));   /* BUNNY.LIN#2 @ 0x21358e */
void bh_goose_0(Obj *o) __attribute__((weak));   /* GOOSE.LIN#0 @ 0x2184aa */
void bh_bos_0(Obj *o) __attribute__((weak));   /* BOS.LIN#0 @ 0x21380a */
void bh_frog_0(Obj *o) __attribute__((weak));   /* FROG.LIN#0 @ 0x2140fc */
void bh_orb_0(Obj *o) __attribute__((weak));   /* ORB.LIN#0 @ 0x216da4 */
void bh_goose_7(Obj *o) __attribute__((weak));   /* GOOSE.LIN#7 @ 0x2144b4 */
void bh_mill_0(Obj *o) __attribute__((weak));   /* MILL.LIN#0 @ 0x2136f4 */
void bh_dada_0(Obj *o) __attribute__((weak));   /* DADA.LIN#0 @ 0x21374c */
void bh_inst1_14(Obj *o) __attribute__((weak));   /* INST1.LIN#14 @ 0x2173ee */
void bh_inst1_11(Obj *o) __attribute__((weak));   /* INST1.LIN#11 @ 0x217530 */
void bh_inst1_9(Obj *o) __attribute__((weak));   /* INST1.LIN#9 @ 0x217674 */
void bh_inst2_2(Obj *o) __attribute__((weak));   /* INST2.LIN#2 @ 0x2176fa */
void bh_inst2_0(Obj *o) __attribute__((weak));   /* INST2.LIN#0 @ 0x217808 */
void bh_inst3_3(Obj *o) __attribute__((weak));   /* INST3.LIN#3 @ 0x21784e */
void bh_inst3_12(Obj *o) __attribute__((weak));   /* INST3.LIN#12 @ 0x217a5a */
void bh_inst4_6(Obj *o) __attribute__((weak));   /* INST4.LIN#6 @ 0x217af6 */
void bh_inst4_0(Obj *o) __attribute__((weak));   /* INST4.LIN#0 @ 0x217b8e */
void bh_inst4_3(Obj *o) __attribute__((weak));   /* INST4.LIN#3 @ 0x217c62 */
void bh_inst5_0(Obj *o) __attribute__((weak));   /* INST5.LIN#0 @ 0x217d88 */
void bh_jeepheli_23(Obj *o) __attribute__((weak));   /* JEEPHELI.LIN#23 @ 0x21698a */
void bh_jeepheli_31(Obj *o) __attribute__((weak));   /* JEEPHELI.LIN#31 @ 0x2169d6 */

static void bh_stub(Obj *o) {   /* unported: stand there with the map graphic */
    enemy_init(o, o->gfxset, 0x22, -16, 1, 10, 1); wait_signal(o);
}

static const struct { uint16_t gfx; Script s; const char *name; const char *addr; } TABLE[] = {
    { 0x0404, bh_foddera_2, "FODDERA.LIN#2", "0x213d68" },
    { 0x0003, bh_medtank_0, "MEDTANK.LIN#0", "0x215bea" },
    { 0x000e, bh_popup_0, "POPUP.LIN#0", "0x2163ce" },
    { 0x180d, bh_rotobase_12, "ROTOBASE.LIN#12", "0x21566e" },
    { 0x0011, bh_mine_0, "MINE.LIN#0", "0x215836" },
    { 0x0013, bh_flame_0, "FLAME.LIN#0", "0x216830" },
    { 0x000f, bh_proxmine_0, "PROXMINE.LIN#0", "0x216700" },
    { 0x0010, bh_train_0, "TRAIN.LIN#0", "0x21589e" },
    { 0x0016, bh_camogun_0, "CAMOGUN.LIN#0", "0x216932" },
    { 0x0014, bh_yellow_0, "YELLOW.LIN#0", "0x2143dc" },
    { 0x0015, bh_bird_0, "BIRD.LIN#0", "0x21434e" },
    { 0x001f, bh_jets_0, "JETS.LIN#0", "0x2159c0" },
    { 0x021f, bh_jets_1, "JETS.LIN#1", "0x215a3e" },
    { 0x0020, bh_blackjet_0, "BLACKJET.LIN#0", "0x2137b8" },
    { 0x003d, bh__onerig_0, "_ONERIG.LIN#0", "0x213e86" },
    { 0x0221, bh_pyramid_1, "PYRAMID.LIN#1", "0x216586" },
    { 0x041d, bh_eggs_2, "EGGS.LIN#2", "0x214198" },
    { 0x041a, bh_diagun_2, "DIAGUN.LIN#2", "0x21648e" },
    { 0x081a, bh_diagun_4, "DIAGUN.LIN#4", "0x2164a8" },
    { 0x0619, bh_destrain_3, "DESTRAIN.LIN#3", "0x215ed0" },
    { 0x0822, bh_trilo_4, "TRILO.LIN#4", "0x213f8a" },
    { 0x1242, bh__plat_9, "_PLAT.LIN#9", "0x2160d2" },
    { 0x1442, bh__plat_10, "_PLAT.LIN#10", "0x2160d8" },
    { 0x0024, bh_truck_0, "TRUCK.LIN#0", "0x215a84" },
    { 0x0023, bh_vtol_0, "VTOL.LIN#0", "0x214064" },
    { 0x1c3c, bh__airport_14, "_AIRPORT.LIN#14", "0x213690" },
    { 0x0e41, bh__corn_7, "_CORN.LIN#7", "0x213f2c" },
    { 0x5600, bh_jeepheli_43, "JEEPHELI.LIN#43", "0x216a50" },
    { 0x0025, bh_mama_0, "MAMA.LIN#0", "0x2138ca" },
    { 0x0027, bh_flattank_0, "FLATTANK.LIN#0", "0x215b24" },
    { 0x0026, bh_tilt_0, "TILT.LIN#0", "0x213b08" },
    { 0x001e, bh_fish_0, "FISH.LIN#0", "0x216ec8" },
    { 0x083e, bh__rigs_4, "_RIGS.LIN#4", "0x216f4a" },
    { 0x042b, bh_juntank_2, "JUNTANK.LIN#2", "0x2162b2" },
    { 0x022b, bh_juntank_1, "JUNTANK.LIN#1", "0x215df2" },
    { 0x0028, bh_lakegun_0, "LAKEGUN.LIN#0", "0x216f8c" },
    { 0x0e28, bh_lakegun_7, "LAKEGUN.LIN#7", "0x216ffc" },
    { 0x0029, bh_lakesub_0, "LAKESUB.LIN#0", "0x21706a" },
    { 0x082a, bh_hover_4, "HOVER.LIN#4", "0x217186" },
    { 0x0a2e, bh_xevious_5, "XEVIOUS.LIN#5", "0x21363a" },
    { 0x122e, bh_xevious_9, "XEVIOUS.LIN#9", "0x213bf8" },
    { 0x002e, bh_xevious_0, "XEVIOUS.LIN#0", "0x216b12" },
    { 0x002f, bh_seaplane_0, "SEAPLANE.LIN#0", "0x2172bc" },
    { 0x0008, bh_edge_0, "EDGE.LIN#0", "0x2134e6" },
    { 0x0009, bh_skyeyea_0, "SKYEYEA.LIN#0", "0x213318" },
    { 0x100a, bh_skyeyeb_8, "SKYEYEB.LIN#8", "0x21340c" },
    { 0x000b, bh_tinytruk_0, "TINYTRUK.LIN#0", "0x216bfc" },
    { 0x0012, bh_airmine_0, "AIRMINE.LIN#0", "0x2132c8" },
    { 0x0030, bh_ski_0, "SKI.LIN#0", "0x215b80" },
    { 0x004e, bh_tap_0, "TAP.LIN#0", "0x215700" },
    { 0x181d, bh_eggs_12, "EGGS.LIN#12", "0x216604" },
    { 0x284c, bh__lava_20, "_LAVA.LIN#20", "0x216cbc" },
    { 0x044f, bh_bunny_2, "BUNNY.LIN#2", "0x21358e" },
    { 0x0017, bh_goose_0, "GOOSE.LIN#0", "0x2184aa" },
    { 0x0050, bh_bos_0, "BOS.LIN#0", "0x21380a" },
    { 0x0053, bh_frog_0, "FROG.LIN#0", "0x2140fc" },
    { 0x0052, bh_orb_0, "ORB.LIN#0", "0x216da4" },
    { 0x0e17, bh_goose_7, "GOOSE.LIN#7", "0x2144b4" },
    { 0x0058, bh_mill_0, "MILL.LIN#0", "0x2136f4" },
    { 0x0059, bh_dada_0, "DADA.LIN#0", "0x21374c" },
    { 0x1c1c, bh_inst1_14, "INST1.LIN#14", "0x2173ee" },
    { 0x161c, bh_inst1_11, "INST1.LIN#11", "0x217530" },
    { 0x121c, bh_inst1_9, "INST1.LIN#9", "0x217674" },
    { 0x042c, bh_inst2_2, "INST2.LIN#2", "0x2176fa" },
    { 0x002c, bh_inst2_0, "INST2.LIN#0", "0x217808" },
    { 0x0631, bh_inst3_3, "INST3.LIN#3", "0x21784e" },
    { 0x1831, bh_inst3_12, "INST3.LIN#12", "0x217a5a" },
    { 0x0c32, bh_inst4_6, "INST4.LIN#6", "0x217af6" },
    { 0x0032, bh_inst4_0, "INST4.LIN#0", "0x217b8e" },
    { 0x0632, bh_inst4_3, "INST4.LIN#3", "0x217c62" },
    { 0x0056, bh_inst5_0, "INST5.LIN#0", "0x217d88" },
    { 0x2e00, bh_jeepheli_23, "JEEPHELI.LIN#23", "0x21698a" },
    { 0x3e00, bh_jeepheli_31, "JEEPHELI.LIN#31", "0x2169d6" },
};

Script eng_handler_for_gfx(uint16_t gfx) {
    for (unsigned i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) if (TABLE[i].gfx == gfx) return TABLE[i].s ? TABLE[i].s : bh_stub;
    return bh_stub;   /* DEFAULT entry */
}
const char *eng_handler_name(uint16_t gfx) {
    for (unsigned i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) if (TABLE[i].gfx == gfx) return TABLE[i].name;
    return "DEFAULT";
}
int eng_handler_ported(uint16_t gfx) {
    for (unsigned i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) if (TABLE[i].gfx == gfx) return TABLE[i].s != NULL;
    return 0;
}
