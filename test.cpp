#include <cpp_channel.h>
#include <random>

std::random_device seed;
std::mt19937 engine(seed());

using namespace std;
using namespace std::chrono_literals;
using namespace chrono;
using ChanPtr = std::shared_ptr<::Chan::Chan>;

std::vector<ChanPtr> sampleChan() {
    std::uniform_int_distribution<> uniformDist(1, 4);
    int chanNum = uniformDist(engine);
    std::vector<ChanPtr> ret;
    for (int i = 0; i < chanNum; i++)
        ret.emplace_back(
            new Chan::Chan{"chan" + std::to_string(i)}
            );
    return ret;
}

bool sampleBernoulli() {
    std::uniform_real_distribution<> uniformReal(0.0, 1.0);
    return uniformReal(engine) > 0.5;
}

std::chrono::milliseconds sampleSleep() {
    std::uniform_int_distribution<> uniformDist(0, 5);
    int ms = uniformDist(engine);
    std::chrono::milliseconds ret = chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ms));
    return ret;
}

using SwitchInstance = vector<tuple<milliseconds, ::Chan::METHOD, ChanPtr>>;

SwitchInstance sampleSwitch(const std::vector<ChanPtr>& chanVec) {
    std::vector<std::tuple<chrono::milliseconds, ::Chan::METHOD, ChanPtr>> ret;
    for (ChanPtr chan : chanVec)
    {
        if (!sampleBernoulli())
            continue;
        Chan::METHOD method = sampleBernoulli() ? ::Chan::METHOD::WRITE : ::Chan::METHOD::READ;
        ret.push_back(make_tuple(sampleSleep(), method, chan));
    }
    return ret;
}

vector<SwitchInstance> sampleTestCase (const std::vector<ChanPtr>& chanVec) {
    vector<SwitchInstance> ret;
    std::uniform_int_distribution<> uniformDist(1, 6);
    int switchNum = uniformDist(engine);
    for (int i = 0; i < switchNum; i++) {
        ret.emplace_back(sampleSwitch(chanVec));
    }
    return ret;
}

void emulate(const vector<SwitchInstance>& switchInstances) {

}

int main() {

    //auto chans = sampleChan();
    sampleSwitch(sampleChan());

    return 0;
}