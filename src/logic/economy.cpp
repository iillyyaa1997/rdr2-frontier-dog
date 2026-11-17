#include "logic/economy.h"

#include <algorithm>
#include <cmath>

#include "core/strutil.h"

namespace fd {

void Economy::load(const KvFile& k) {
    ordinaryShare = k.getFloat("price.ordinary_share", ordinaryShare);
    pedigreeShare = k.getFloat("price.pedigree_share", pedigreeShare);
    muttOrdinaryPrice = int(k.getInt("price.mutt_ordinary", muttOrdinaryPrice));
    hardenedValue = int(k.getInt("value.hardened", hardenedValue));
    ailingValue = int(k.getInt("value.ailing", ailingValue));
    sellWithPapers = k.getFloat("sell.with_papers", sellWithPapers);
    sellWithoutPapers = k.getFloat("sell.without_papers", sellWithoutPapers);
    papersPrice = int(k.getInt("papers.price", papersPrice));
    papersPricePedigree = int(k.getInt("papers.price_pedigree", papersPricePedigree));
    curePrice = int(k.getInt("vet.cure_price", curePrice));
    cureDays = int(k.getInt("vet.cure_days", cureDays));
    pedigreePrice = int(k.getInt("vet.pedigree_price", pedigreePrice));
    pedigreeDays = int(k.getInt("vet.pedigree_days", pedigreeDays));
    renamePrice = int(k.getInt("rename.price", renamePrice));
    collarPrice = int(k.getInt("collar.price", collarPrice));
    swapPrice = int(k.getInt("swap.price", swapPrice));
    swapBringSeconds = int(k.getInt("swap.bring_seconds", swapBringSeconds));
    deliveryMin = int(k.getInt("delivery.min", deliveryMin));
    deliveryMax = int(k.getInt("delivery.max", deliveryMax));
    deliveryPerKm = k.getFloat("delivery.per_km", deliveryPerKm);
    messengerPrice = int(k.getInt("delivery.messenger", messengerPrice));
    if (k.has("training.price")) {
        trainingPrice.clear();
        for (double v : k.getFloats("training.price")) trainingPrice.push_back(int(v));
    }
    if (k.has("training.factor")) trainingFactor = k.getFloats("training.factor");
    stockSize = int(k.getInt("shop.stock", stockSize));
    rotationDays = std::max(1, int(k.getInt("shop.rotation_days", rotationDays)));
    shopPedigreeChance = k.getFloat("shop.pedigree", shopPedigreeChance);
}

static int roundTo5(double v) { return std::max(1, int(std::lround(v / 5.0)) * 5); }

int shopPrice(const BreedDef& b, Grade g, const Economy& e) {
    if (b.mutt) return e.muttOrdinaryPrice;
    if (g == Grade::Pedigree) return roundTo5(b.price * e.pedigreeShare);
    return roundTo5(b.price * e.ordinaryShare);
}

int dogValue(const DogRecord& d, const BreedTable& t, const Economy& e) {
    const BreedDef* b = t.find(d.breed);
    if (!b) return e.ailingValue;
    if (d.grade == Grade::Ailing) return e.ailingValue;
    if (b->mutt) return d.grade == Grade::Pedigree ? e.hardenedValue : e.muttOrdinaryPrice;
    return shopPrice(*b, d.grade, e);
}

int sellPrice(const DogRecord& d, const BreedTable& t, const Economy& e) {
    double share = d.papers ? e.sellWithPapers : e.sellWithoutPapers;
    return std::max(1, int(std::lround(dogValue(d, t, e) * share)));
}

int papersPrice(const DogRecord& d, const Economy& e) {
    return d.grade == Grade::Pedigree ? e.papersPricePedigree : e.papersPrice;
}

int trainingPrice(const DogRecord& d, const std::string& skill, const Economy& e) {
    int lvl = d.skill(skill);
    if (lvl >= 3 || lvl < 1 || lvl >= int(e.trainingPrice.size())) return -1;
    int tr = std::clamp(d.stats[Trainability], 1, 5);
    double f = tr - 1 < int(e.trainingFactor.size()) ? e.trainingFactor[tr - 1] : 1.0;
    return std::max(1, int(std::lround(e.trainingPrice[lvl] * f)));
}

int deliveryFee(double meters, const Economy& e) {
    int fee = int(std::lround(e.deliveryMin + meters / 1000.0 * e.deliveryPerKm));
    return std::clamp(fee, e.deliveryMin, e.deliveryMax);
}

std::vector<StockDog> stableStock(const std::string& stableId, const std::vector<std::string>& breedIds, long long gameDay,
                                  const BreedTable& t, const Economy& e, const RollChances& c,
                                  const std::vector<std::string>& disabled) {
    std::vector<StockDog> out;
    long long period = gameDay / e.rotationDays;
    Rng rng((uint64_t(str::joaat(stableId.c_str())) << 32) ^ uint64_t(period * 2654435761ULL));
    std::vector<const BreedDef*> pool;
    for (const auto& id : breedIds) {
        const BreedDef* b = t.find(id);
        if (b && !b->strayOnly && std::find(disabled.begin(), disabled.end(), id) == disabled.end()) pool.push_back(b);
    }
    // the stable shows stockSize breeds of its regional list, a different selection each period (D19: 3 of 4)
    for (int n = 0; n < e.stockSize && !pool.empty(); ++n) {
        int i = rng.range(0, int(pool.size()) - 1);
        const BreedDef* b = pool[i];
        pool.erase(pool.begin() + i);
        Grade g = (!b->mutt && rng.chance(e.shopPedigreeChance)) ? Grade::Pedigree : Grade::Ordinary;
        StockDog s;
        s.dog = makeDog(*b, t, g, rng, c, {});
        s.dog.papers = true;  // shop dogs come with papers (D118)
        s.price = shopPrice(*b, g, e);
        out.push_back(s);
    }
    return out;
}

}  // namespace fd
