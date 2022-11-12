#include <cpp_channel.h>
#include <random>
#include <set>

std::random_device seed;
//std::mt19937 engine(seed());
std::mt19937 engine(5);

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

using SwitchInstance = vector<tuple<string, milliseconds, ::Chan::METHOD, Chan::Chan*>>;

SwitchInstance sampleSwitch(const std::string& name, const std::vector<ChanPtr>& chanVec) {
    SwitchInstance ret;
    for (ChanPtr chan : chanVec)
    {
        if (!sampleBernoulli())
            continue;
        Chan::METHOD method = sampleBernoulli() ? ::Chan::METHOD::WRITE : ::Chan::METHOD::READ;
        ret.push_back(make_tuple(name, sampleSleep(), method, chan.get()));
    }
    return ret;
}

vector<SwitchInstance> sampleTestCase (const std::vector<ChanPtr>& chanVec) {
    vector<SwitchInstance> ret;
    std::uniform_int_distribution<> uniformDist(1, 6);
    int switchNum = uniformDist(engine);
    for (int i = 0; i < switchNum; i++) {
        string name = "Switch" + to_string(i);
        ret.emplace_back(sampleSwitch(name, chanVec));
    }
    return ret;
}

void doEmulate(const vector<SwitchInstance>& switchInstances, 
                int pos,
                set<const SwitchInstance*>& cur, //alive switch
                set<set<const SwitchInstance*>>& res
                ) {
    if (pos == switchInstances.size()) {
        res.insert(cur);
        return;
    }
    map<pair<Chan::METHOD, Chan::Chan *>, vector<const SwitchInstance *>> wait2switchIns;
    for (const SwitchInstance* pSwitchIns : cur) {
        for (auto& [name, _, method, pChan] : *pSwitchIns) {
            wait2switchIns[make_pair(method, pChan)].push_back(pSwitchIns);
        }
    }

    const SwitchInstance* pSwitchInstance = &switchInstances[pos];
    bool found = false;
    for (auto &[name, _, method, pChan] : *pSwitchInstance)
    {
        Chan::METHOD needMethod = (method == Chan::METHOD::READ ? Chan::METHOD::WRITE : Chan::METHOD::READ);
        auto key = make_pair(needMethod, pChan);
        if (wait2switchIns.find(key) != wait2switchIns.end()) {
            found = true;
            for (const SwitchInstance* pMatchedSwitchInstance : wait2switchIns.at(key)) {
                cur.erase(pMatchedSwitchInstance);
                doEmulate(switchInstances, pos + 1, cur, res);
                cur.insert(pMatchedSwitchInstance);
            }
        }
    }
    if (!found) {
        cur.insert(pSwitchInstance);
        doEmulate(switchInstances, pos + 1, cur, res);
        cur.erase(pSwitchInstance);
    }
}

set<Chan::NamedStatus> emulate(const vector<SwitchInstance>& switchInstances) {
    set<const SwitchInstance *> cur;
    set<set<const SwitchInstance *>> res;
    doEmulate(switchInstances, 0, cur, res);

    set<Chan::NamedStatus> ret;
    for (const set<const SwitchInstance *> &switchInstanceSet : res) {
        Chan::NamedStatus status;
        for (const SwitchInstance *pSwitchInstance : switchInstanceSet) {
            for (auto& [name, _, method, pChan] : *pSwitchInstance) {
                status.emplace_back(name, method, pChan->getName());
            }
        }
        ret.insert(status);
    }
    return ret;
}

vector<Chan::NamedStatus> getInitNameStatus(const vector<SwitchInstance>& switchInstances) {
    vector<Chan::NamedStatus> ret;
    for (auto& switchInstance : switchInstances) {
        Chan::NamedStatus namedStatus;
        for (auto &[name, _, method, pChan] : switchInstance)
        {
            namedStatus.emplace_back(name, method, pChan->getName());
        }
        ret.emplace_back(namedStatus);
    }
    return ret;
}

int main() {

    std::vector<ChanPtr> chans = sampleChan();
    vector<SwitchInstance> testCase = sampleTestCase(chans);
    for(auto &i : getInitNameStatus(testCase)) {
        printNamedStatus(i);
    }

    set<Chan::NamedStatus> emulateResult = emulate(testCase);
    std::cout << testCase.size() << " " << emulateResult.size() << std::endl;
    for (auto &i : emulateResult)
    {
        printNamedStatus(i);
    }
    return 0;
}