// Prices and the stables' stock (plan D112, D117, D118, D120, D124, D115). Every number comes from Economy.dat.
#pragma once
#include <string>
#include <vector>

#include "core/kvfile.h"
#include "logic/dog.h"

namespace fd {

struct Economy {
    double ordinaryShare = 0.40;     // ordinary dog = 40 % of the breed price
    double pedigreeShare = 1.25;     // pedigree = breed price × 1.25
    int muttOrdinaryPrice = 15;
    double dogPriceScale = 1.0;      // DogPricePercent in FrontierDog.ini / 100 (Nexus 22.11: economy mods)
    int hardenedValue = 150;         // street-hardened mutt (never sold in shops)
    int ailingValue = 10;
    double sellWithPapers = 0.60;
    double sellWithoutPapers = 0.30;
    int papersPrice = 10, papersPricePedigree = 25;
    int curePrice = 40, cureDays = 1;
    int pedigreePrice = 150, pedigreeDays = 7;
    int renamePrice = 2;
    int collarPrice = 8;             // a collar from the stable hand (17.11)
    int swapPrice = 5, swapBringSeconds = 15;
    int deliveryMin = 5, deliveryMax = 15;  // by distance
    double deliveryPerKm = 2.0;
    int messengerPrice = 2;
    std::vector<int> trainingPrice = {0, 40, 80};     // to L2, to L3 (index = current level)
    std::vector<double> trainingFactor = {1.5, 1.25, 1.0, 0.85, 0.7};  // by trainability 1-5 (old D61)
    int stockSize = 3, rotationDays = 3;
    double shopPedigreeChance = 0.25;

    void load(const KvFile& kv);
};

int shopPrice(const BreedDef& b, Grade g, const Economy& e);
int dogValue(const DogRecord& d, const BreedTable& t, const Economy& e);   // what the dog is worth (before papers)
int sellPrice(const DogRecord& d, const BreedTable& t, const Economy& e);
int papersPrice(const DogRecord& d, const Economy& e);
int trainingPrice(const DogRecord& d, const std::string& skill, const Economy& e);  // -1 if already at L3
int deliveryFee(double distanceMeters, const Economy& e);

// A stable's dogs for sale in the rotation period that contains gameDay: deterministic per stable and period, so the
// same dogs stand there all period long and after a reload.
struct StockDog {
    DogRecord dog;
    int price = 0;
};
std::vector<StockDog> stableStock(const std::string& stableId, const std::vector<std::string>& breedIds, long long gameDay,
                                  const BreedTable& t, const Economy& e, const RollChances& c,
                                  const std::vector<std::string>& disabledBreeds);

}  // namespace fd
