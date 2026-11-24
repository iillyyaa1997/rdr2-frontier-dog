#include "dog/coat.h"

namespace fd::coat {

void dress(Ped ped, const DogRecord& d) {
    if (!ped) return;
    if (d.preset >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, d.preset, false);
}

}  // namespace fd::coat
