#include <channel.h>
#include <random>
#include <set>
#include <functional>

//std::random_device seed;
//std::mt19937 engine(seed());

using namespace std;
using namespace std::chrono_literals;
using namespace chrono;
using ChanPtr = std::shared_ptr<::Channel::Chan>;

std::vector<ChanPtr> sampleChan(std::mt19937& engine) {
    std::uniform_int_distribution<> uniformDist(1, 4);
    int chanNum = uniformDist(engine);
    std::vector<ChanPtr> ret;
    for (int i = 0; i < chanNum; i++)
        ret.emplace_back(
            new Channel::Chan{"chan" + std::to_string(i)}
        );
    return ret;
}

bool sampleBernoulli(std::mt19937& engine) {
    std::uniform_real_distribution<> uniformReal(0.0, 1.0);
    return uniformReal(engine) > 0.5;
}

std::chrono::microseconds sampleSleep(std::mt19937& engine) {
    std::uniform_int_distribution<> uniformDist(0, 5);
    std::chrono::microseconds ret = chrono::microseconds(uniformDist(engine) * 100);
    return ret;
}

using CaseInstance = tuple<microseconds, ::Channel::METHOD, Channel::Chan *, std::shared_ptr<int>>;
using SelectInstance = pair<string,vector<CaseInstance>>;
using TestCase = vector<pair<microseconds, SelectInstance>>;

SelectInstance sampleSelect(std::mt19937& engine, const std::string& name, const std::vector<Channel::Chan*>& chanVec) {
    SelectInstance ret;
    ret.first = name;
    for (Channel::Chan *pChan : chanVec) {
        if (!sampleBernoulli(engine))
            continue;
        Channel::METHOD method = sampleBernoulli(engine) ? ::Channel::METHOD::WRITE : ::Channel::METHOD::READ;
        int val = std::uniform_int_distribution<>(0, 99)(engine);
        ret.second.push_back(make_tuple(sampleSleep(engine), method, pChan, make_shared<int>(val)));
    }
    return ret;
}

TestCase sampleTestCase (std::mt19937& engine, const std::vector<Channel::Chan*>& chanVec) {
    TestCase ret;
    std::uniform_int_distribution<> uniformDist(1, 6);
    int selectNum = uniformDist(engine);
    std::chrono::microseconds accSleepTime(0s);
    for (int i = 0; i < selectNum; i++) {
        string name = "Select" + to_string(i);
        accSleepTime += sampleSleep(engine);
        cout << "select" << i << "sleep:" << accSleepTime.count() / 1000 << "ms" << endl;
        ret.emplace_back(accSleepTime, sampleSelect(engine, name, chanVec));
    }
    return ret;
}

pair<const  SelectInstance *, const SelectInstance *> makeOrderedSelectPair(const SelectInstance *a, const SelectInstance *b) {
    return a < b ? make_pair(a, b) : make_pair(b, a);
}
void recursiveEmulate(vector<pair<pair<const SelectInstance *, const SelectInstance *>, bool>>& pSelectPairVec,
                      map<const SelectInstance*, bool>& usedSelect,
                      int pos,
                      set<Channel::NamedStatus>& res) {
    if (pos >= pSelectPairVec.size()) {
        for (pair<pair<const SelectInstance *, const SelectInstance *>, bool>& i : pSelectPairVec) {
            if (i.second)
                continue;
            if (!usedSelect[i.first.first] && !usedSelect[i.first.second])
                return;
        }
        Channel::NamedStatus status;
        for (auto& [pSelect, flag] : usedSelect) {
            if (flag)
                continue;
            string selectName = pSelect->first;
            for (auto &[_, method, pChan, __] : pSelect->second) {
                status.insert(make_tuple(selectName, method, pChan->getName()));
            }
        }
        res.insert(status);
        return;
    }
    recursiveEmulate(pSelectPairVec, usedSelect, pos + 1, res);
    pair<pair<const SelectInstance *, const SelectInstance *>, bool>& pSelect = pSelectPairVec.at(pos);
    const SelectInstance * pSelectA = pSelect.first.first;
    const SelectInstance * pSelectB = pSelect.first.second;
    if (!usedSelect[pSelectA] && !usedSelect[pSelectB]) {
        usedSelect[pSelectA] = true;
        usedSelect[pSelectB] = true;
        pSelect.second = true;
        recursiveEmulate(pSelectPairVec, usedSelect, pos + 1, res);
        usedSelect[pSelectA] = false;
        usedSelect[pSelectB] = false;
        pSelect.second = false;
    }
}
set<Channel::NamedStatus> emulate(const TestCase &selectInstances) {
    map<pair<Channel::Chan *, Channel::METHOD>, vector<const SelectInstance*>> chanMethod2Select;
    map<const SelectInstance*, bool> usedSelect;

    for (auto& selectInstance : selectInstances) {
        const SelectInstance* pSelectIns = &selectInstance.second;
        usedSelect[pSelectIns] = false;
        for (auto &[_, method, pChan, __] : pSelectIns->second) {
            chanMethod2Select[make_pair(pChan, method)].emplace_back(pSelectIns);
        }
    }

    set<pair<const SelectInstance *, const SelectInstance *>> pSelectPairSet;
    for (auto &[pair_, selectVecA] : chanMethod2Select) {
        auto methodB = (pair_.second == Channel::METHOD::READ) ? Channel::WRITE : Channel::READ;
        auto &selectVecB = chanMethod2Select[make_pair(pair_.first, methodB)];
        for (const SelectInstance* pSelectA : selectVecA) {
            for (const SelectInstance* pSelectB : selectVecB) {
                pSelectPairSet.insert(makeOrderedSelectPair(pSelectA, pSelectB));
            }
        }
    }

    vector<pair<pair<const SelectInstance *, const SelectInstance *>, bool>> pSelectPairVec;
    transform(pSelectPairSet.begin(), pSelectPairSet.end(), back_insert_iterator(pSelectPairVec), [](const auto &a) {
        return make_pair(a, false);
    });
    set<Channel::NamedStatus> res;
    recursiveEmulate(pSelectPairVec, usedSelect, 0, res);
    return res;
}

vector<Channel::NamedStatus> getInitNameStatus(TestCase& selectInstances) {
    vector<Channel::NamedStatus> ret;
    for (auto& [selectSleepTime, selectInstance] : selectInstances) {
        Channel::NamedStatus namedStatus;
        string selectName = selectInstance.first;
        for (auto &[_, method, pChan, __] : selectInstance.second) {
            namedStatus.insert(make_tuple(selectName, method, pChan->getName()));
        }
        ret.emplace_back(namedStatus);
    }
    return ret;
}

auto taskFun = [](const microseconds &sleepTime, Channel::METHOD method) -> Channel::Task {
    auto retFun = [=](const std::string &selectName, const std::string &chanName,
    int a) {
        this_thread::sleep_for(sleepTime);
        log("###%s:%s:%s:%d\n", selectName.c_str(), ((method == Channel::METHOD::READ) ? "read" : "write"), chanName.c_str(), a);
        return true;
    };
    return retFun;
};

Channel::NamedStatus executeTestCase(const vector<Channel::Chan *> &chanVec, const TestCase& testCase) {
    vector<thread> threadPool;
    for (auto& [selectSleepTime, selectInstance] : testCase) {
        std::vector<Channel::Case> caseVec;
        string selectName = selectInstance.first;
        for (const tuple<microseconds, ::Channel::METHOD, Channel::Chan *, shared_ptr<int>> &caseTup : selectInstance.second) {
            microseconds caseSleepTime = get<0>(caseTup);
            Channel::METHOD method = get<1>(caseTup);
            caseVec.emplace_back(method, get<2>(caseTup), get<3>(caseTup).get(), taskFun(microseconds(0), method));
        }
        auto threadFun = [](string selectName, std::vector<Channel::Case> caseVec, microseconds selectSleepTime) {
            this_thread::sleep_for(selectSleepTime);
            printf("%s start\n", selectName.c_str());
            Channel::Select(selectName, caseVec.begin(), caseVec.end());
        };
        threadPool.emplace_back(threadFun, selectName, caseVec, selectSleepTime);
    }
    this_thread::sleep_for(2s);
    Channel::NamedStatus namedStatus = Channel::watchNamedStatus(chanVec);
    Channel::printNamedStatus(namedStatus);
    for (auto &t : threadPool)
        t.detach();
    return namedStatus;
}

void testcase(std::mt19937& engine) {
    std::vector<ChanPtr> chans = sampleChan(engine);
    std::vector<Channel::Chan *> chanVec;
    transform(chans.begin(), chans.end(), std::back_inserter(chanVec), [](auto &c) {
        return c.get();
    });
    TestCase testCase = sampleTestCase(engine, chanVec);
    for(auto &i : getInitNameStatus(testCase)) {
        printNamedStatus(i);
    }

    set<Channel::NamedStatus> emulateResult = emulate(testCase);
    std::cout << "Select num:" << testCase.size() << " result size:" << emulateResult.size() << std::endl;
    cout << "expected results:" << endl;
    for (auto &i : emulateResult) {
        printNamedStatus(i);
    }

    Channel::NamedStatus runResult = executeTestCase(chanVec, testCase);
    cout << "got result:" << endl;
    printNamedStatus(runResult);
    assert(emulateResult.find(runResult) != emulateResult.end());
    return;
}
int main() {
    for (int i = 0; i < 100; i++) {
        std::cout << "rand number:" << i << std::endl;
        std::mt19937 engine(i);
        testcase(engine);
    }
    return 0;
}