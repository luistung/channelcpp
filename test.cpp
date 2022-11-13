#include <channel.h>
#include <random>
#include <set>

std::random_device seed;
//std::mt19937 engine(seed());
std::mt19937 engine(5);

using namespace std;
using namespace std::chrono_literals;
using namespace chrono;
using ChanPtr = std::shared_ptr<::Channel::Chan>;

std::vector<ChanPtr> sampleChan() {
    std::uniform_int_distribution<> uniformDist(1, 4);
    int chanNum = uniformDist(engine);
    std::vector<ChanPtr> ret;
    for (int i = 0; i < chanNum; i++)
        ret.emplace_back(
            new Channel::Chan{"chan" + std::to_string(i)}
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

using SelectInstance = vector<tuple<string, milliseconds, ::Channel::METHOD, Channel::Chan*>>;

SelectInstance sampleSelect(const std::string& name, const std::vector<ChanPtr>& chanVec) {
    SelectInstance ret;
    for (ChanPtr chan : chanVec)
    {
        if (!sampleBernoulli())
            continue;
        Channel::METHOD method = sampleBernoulli() ? ::Channel::METHOD::WRITE : ::Channel::METHOD::READ;
        ret.push_back(make_tuple(name, sampleSleep(), method, chan.get()));
    }
    return ret;
}

vector<SelectInstance> sampleTestCase (const std::vector<ChanPtr>& chanVec) {
    vector<SelectInstance> ret;
    std::uniform_int_distribution<> uniformDist(1, 6);
    int selectNum = uniformDist(engine);
    for (int i = 0; i < selectNum; i++) {
        string name = "Select" + to_string(i);
        ret.emplace_back(sampleSelect(name, chanVec));
    }
    return ret;
}

void doEmulate(const vector<SelectInstance>& selectInstances, 
                int pos,
                set<const SelectInstance*>& cur, //alive select
                set<set<const SelectInstance*>>& res
                ) {
    if (pos == selectInstances.size()) {
        res.insert(cur);
        return;
    }
    map<pair<Channel::METHOD, Channel::Chan *>, vector<const SelectInstance *>> wait2selectIns;
    for (const SelectInstance* pSelectIns : cur) {
        for (auto& [name, _, method, pChan] : *pSelectIns) {
            wait2selectIns[make_pair(method, pChan)].push_back(pSelectIns);
        }
    }

    const SelectInstance* pSelectInstance = &selectInstances[pos];
    bool found = false;
    for (auto &[name, _, method, pChan] : *pSelectInstance)
    {
        Channel::METHOD needMethod = (method == Channel::METHOD::READ ? Channel::METHOD::WRITE : Channel::METHOD::READ);
        auto key = make_pair(needMethod, pChan);
        if (wait2selectIns.find(key) != wait2selectIns.end()) {
            found = true;
            for (const SelectInstance* pMatchedSelectInstance : wait2selectIns.at(key)) {
                cur.erase(pMatchedSelectInstance);
                doEmulate(selectInstances, pos + 1, cur, res);
                cur.insert(pMatchedSelectInstance);
            }
        }
    }
    if (!found) {
        cur.insert(pSelectInstance);
        doEmulate(selectInstances, pos + 1, cur, res);
        cur.erase(pSelectInstance);
    }
}

set<Channel::NamedStatus> emulate(const vector<SelectInstance>& selectInstances) {
    set<const SelectInstance *> cur;
    set<set<const SelectInstance *>> res;
    doEmulate(selectInstances, 0, cur, res);

    set<Channel::NamedStatus> ret;
    for (const set<const SelectInstance *> &selectInstanceSet : res) {
        Channel::NamedStatus status;
        for (const SelectInstance *pSelectInstance : selectInstanceSet) {
            for (auto& [name, _, method, pChan] : *pSelectInstance) {
                status.emplace_back(name, method, pChan->getName());
            }
        }
        ret.insert(status);
    }
    return ret;
}

vector<Channel::NamedStatus> getInitNameStatus(const vector<SelectInstance>& selectInstances) {
    vector<Channel::NamedStatus> ret;
    for (auto& selectInstance : selectInstances) {
        Channel::NamedStatus namedStatus;
        for (auto &[name, _, method, pChan] : selectInstance)
        {
            namedStatus.emplace_back(name, method, pChan->getName());
        }
        ret.emplace_back(namedStatus);
    }
    return ret;
}

int main() {

    std::vector<ChanPtr> chans = sampleChan();
    vector<SelectInstance> testCase = sampleTestCase(chans);
    for(auto &i : getInitNameStatus(testCase)) {
        printNamedStatus(i);
    }

    set<Channel::NamedStatus> emulateResult = emulate(testCase);
    std::cout << testCase.size() << " " << emulateResult.size() << std::endl;
    for (auto &i : emulateResult)
    {
        printNamedStatus(i);
    }
    return 0;
}