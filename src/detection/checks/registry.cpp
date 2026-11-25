// Registry of all checks, in display order.

#include "../engine.h"

namespace vigil::detection {

std::unique_ptr<Check> makeKillAura();
std::unique_ptr<Check> makeReach();
std::unique_ptr<Check> makeAutoClicker();
std::unique_ptr<Check> makeVelocity();
std::unique_ptr<Check> makeAutoBlock();
std::unique_ptr<Check> makeSpeed();
std::unique_ptr<Check> makeBHop();
std::unique_ptr<Check> makeNoSlow();
std::unique_ptr<Check> makeKeepY();
std::unique_ptr<Check> makeFlatScaffold();
std::unique_ptr<Check> makeLegitScaff();

std::vector<std::unique_ptr<Check>> createAllChecks() {
    std::vector<std::unique_ptr<Check>> v;
    v.push_back(makeKillAura());
    v.push_back(makeReach());
    v.push_back(makeAutoClicker());
    v.push_back(makeVelocity());
    v.push_back(makeAutoBlock());
    v.push_back(makeSpeed());
    v.push_back(makeBHop());
    v.push_back(makeNoSlow());
    v.push_back(makeKeepY());
    v.push_back(makeFlatScaffold());
    v.push_back(makeLegitScaff());
    return v;
}

}  // namespace vigil::detection
