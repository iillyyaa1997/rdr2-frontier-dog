#include "dog/store.h"

#include "core/fs.h"
#include "core/log.h"

namespace fd {

std::string DogStore::path(int slot) const { return fs::join(dir_, std::to_string(slot + 1) + ".dat"); }

void DogStore::open(const std::string& dogsDir) {
    dir_ = dogsDir;
    fs::makeDirs(dir_);
    for (int i = 0; i < kSlots; ++i) {
        KvFile kv;
        used_[i] = kv.load(path(i)) && !kv.get("breed").empty();
        if (used_[i]) {
            dogs_[i].fromKv(kv);
            FD_INFO("dog %d: %s (%s), grade %d, where %d", i + 1, dogs_[i].name.c_str(), dogs_[i].breed.c_str(),
                    int(dogs_[i].grade), int(dogs_[i].where));
        }
    }
    // never two dogs with Arthur: keep the first, send the others home
    bool seen = false;
    for (int i = 0; i < kSlots; ++i) {
        if (!used_[i] || dogs_[i].where != Where::WithArthur) continue;
        if (seen) {
            dogs_[i].where = Where::Home;
            save(i);
        }
        seen = true;
    }
}

int DogStore::withArthur() const {
    for (int i = 0; i < kSlots; ++i)
        if (used_[i] && dogs_[i].where == Where::WithArthur) return i;
    return -1;
}

int DogStore::freeSlot() const {
    for (int i = 0; i < kSlots; ++i)
        if (!used_[i]) return i;
    return -1;
}

int DogStore::count() const {
    int n = 0;
    for (bool u : used_) n += u;
    return n;
}

int DogStore::put(const DogRecord& d) {
    int s = freeSlot();
    if (s < 0) return -1;
    dogs_[s] = d;
    used_[s] = true;
    save(s);
    return s;
}

bool DogStore::save(int slot) {
    if (slot < 0 || slot >= kSlots || !used_[slot]) return false;
    bool ok = dogs_[slot].toKv().save(path(slot));
    if (!ok) FD_ERROR("could not save %s", path(slot).c_str());
    return ok;
}

void DogStore::remove(int slot) {
    if (slot < 0 || slot >= kSlots || !used_[slot]) return;
    fs::remove(path(slot) + ".gone");
    fs::copy(path(slot), path(slot) + ".gone");
    fs::remove(path(slot));
    used_[slot] = false;
    dogs_[slot] = DogRecord{};
}

}  // namespace fd
