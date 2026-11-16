// The four dogs (D98): FrontierDog\Dogs\1.dat .. 4.dat. One may be with Arthur, the others at home or away.
#pragma once
#include <array>
#include <string>

#include "logic/dog.h"

namespace fd {

class DogStore {
public:
    static constexpr int kSlots = 4;

    void open(const std::string& dogsDir);
    bool used(int slot) const { return used_[slot]; }
    DogRecord& at(int slot) { return dogs_[slot]; }
    const DogRecord& at(int slot) const { return dogs_[slot]; }
    int withArthur() const;      // slot or -1
    int freeSlot() const;        // -1 when all four are taken
    int count() const;
    int put(const DogRecord& d); // into a free slot, saved; -1 if full
    bool save(int slot);
    void remove(int slot);       // the dog is gone (sold, died for good): file kept as <n>.dat.gone for safety

private:
    std::string path(int slot) const;
    std::string dir_;
    std::array<DogRecord, kSlots> dogs_{};
    std::array<bool, kSlots> used_{};
};

}  // namespace fd
