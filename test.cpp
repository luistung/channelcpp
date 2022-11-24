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
    for (int i = 0; i < chanNum; i++) {
        int bufferSize = std::uniform_int_distribution<>(0, 2)(engine);
        ret.emplace_back(new Channel::Chan{bufferSize, "chan" + std::to_string(i)});
    }
    return ret;
}

bool sampleBernoulli(std::mt19937& engine) {
    std::uniform_real_distribution<> uniformReal(0.0, 1.0);
    return uniformReal(engine) > 0.5;
}

std::chrono::microseconds sleepTimeSum = std::chrono::microseconds{};
std::chrono::microseconds sampleSleep(std::mt19937 &engine) {
    std::uniform_int_distribution<> uniformDist(0, 5);
    std::chrono::microseconds sleepTime= chrono::microseconds(uniformDist(engine) * 100);
    sleepTimeSum += sleepTime;
    std::chrono::microseconds ret = sleepTime;
    return ret;
}

using CaseInstance = tuple<microseconds, ::Channel::METHOD, Channel::Chan *, std::shared_ptr<int>>;
using SelectInstance = tuple<string,vector<CaseInstance>, bool>;
using TestCase = vector<pair<microseconds, SelectInstance>>;

SelectInstance sampleSelect(std::mt19937& engine, const std::string& name, const std::vector<Channel::Chan*>& chanVec) {
    while (1) {
        SelectInstance ret;
        get<0>(ret) = name;
        for (Channel::Chan *pChan : chanVec) {
            if (!sampleBernoulli(engine))
                continue;
            Channel::METHOD method = sampleBernoulli(engine) ? ::Channel::METHOD::WRITE : ::Channel::METHOD::READ;
            int val = std::uniform_int_distribution<>(0, 99)(engine);
            get<1>(ret).push_back(make_tuple(sampleSleep(engine), method, pChan, make_shared<int>(val)));
        }
        if (!get<1>(ret).empty()) {
            get<2>(ret) = sampleBernoulli(engine);
            return ret;
        }
    }

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
            string selectName = get<0>(*pSelect);
            for (auto &[_, method, pChan, __] : get<1>(*pSelect)) {
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
set<Channel::NamedStatus> emulate2(const TestCase &selectInstances) {
    map<pair<Channel::Chan *, Channel::METHOD>, vector<const SelectInstance*>> chanMethod2Select;
    map<const SelectInstance*, bool> usedSelect;

    for (auto& selectInstance : selectInstances) {
        const SelectInstance* pSelectIns = &selectInstance.second;
        usedSelect[pSelectIns] = false;
        for (auto &[_, method, pChan, __] : get<1>(*pSelectIns)) {
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
    transform(pSelectPairSet.begin(), pSelectPairSet.end(), std::back_insert_iterator(pSelectPairVec), [](const auto &a) {
        return make_pair(a, false);
    });
    set<Channel::NamedStatus> res;
    recursiveEmulate(pSelectPairVec, usedSelect, 0, res);
    return res;
}

void doEmulate(const TestCase &selectInstances,
               map<Channel::Chan *, int>& chan2Qsize,
               std::vector<int>& consumedSelect,
               std::vector<bool>& consumedFlag,
               map<pair<Channel::Chan*, Channel::METHOD>, set<int>>& blockSelect,
               set<Channel::NamedStatus>& results) {
    /*cout << "consumed:";
    for (int i : consumedSelect) cout << i << ",";
    cout << endl;
    cout << "flag:";
    for (bool i : consumedFlag) cout << i << ",";
    cout << endl;

    cout << "block select:";
    for (auto& [key, value]:blockSelect) {
        for (int i : value) cout << i << " " << key.first << " " << key.second << endl;
    }
    cout << endl;
    cout << endl;*/

    if (consumedSelect.size() >=
            selectInstances.size()) {
        Channel::NamedStatus result;
        for (auto &[chan2method, selectSet] : blockSelect) {
            for (int selectPos : selectSet) {
                auto tup = make_tuple(get<0>(selectInstances[selectPos].second),
                                      chan2method.second, chan2method.first->getName());
                result.insert(tup);
            }
        }
        results.insert(result);
        return;
    }
    for (int i = 0; i < selectInstances.size(); i++) {
        if (consumedFlag[i]) continue;
        consumedFlag[i] = true;
        consumedSelect.push_back(i);
        bool hasMatchBlock = false;
        //check blocked select
        for (const CaseInstance &c : get<1>(selectInstances.at(i).second)) {
            Channel::Chan *pChan = get<2>(c);
            Channel::METHOD method = get<1>(c);
            Channel::METHOD needMethod = (method == Channel::METHOD::READ) ? Channel::METHOD::WRITE : Channel::METHOD::READ;


            set<int> &blockSelectSet = blockSelect[make_pair(pChan, needMethod)];
            if (!blockSelectSet.empty()) hasMatchBlock = true;
            else
                continue;
            vector<int> selectPoses;
            transform(blockSelectSet.begin(), blockSelectSet.end(),
                      back_insert_iterator(selectPoses),
            [](int a) {
                return a;
            });
            for (int peerSelectPos : selectPoses) {
                vector<pair<pair<Channel::Chan *, Channel::METHOD>, int>>
                        removedItems;
                for (auto &[key, selectSet] : blockSelect) {
                    if (selectSet.erase(peerSelectPos) != 0) {
                        removedItems.emplace_back(key, peerSelectPos);
                    }
                }

                doEmulate(selectInstances, chan2Qsize, consumedSelect,
                          consumedFlag, blockSelect, results);

                for (auto& [key, removedPos] : removedItems) {
                    blockSelect[key].insert(removedPos);
                }
            }
        }
        //buffered
        if (!hasMatchBlock) {
            bool hasBuffer = false;
            for (const CaseInstance &c : get<1>(selectInstances.at(i).second)) {
                Channel::Chan *pChan = get<2>(c);
                Channel::METHOD method = get<1>(c);
                if (method == Channel::METHOD::READ && chan2Qsize[pChan] > 0) {
                    hasBuffer = true;
                    chan2Qsize[pChan]--;
                    doEmulate(selectInstances, chan2Qsize, consumedSelect,
                              consumedFlag, blockSelect, results);
                    chan2Qsize[pChan]++;
                }
                if (method == Channel::METHOD::WRITE && chan2Qsize[pChan] < pChan->getCapacity()) {
                    hasBuffer = true;
                    chan2Qsize[pChan]++;
                    doEmulate(selectInstances, chan2Qsize, consumedSelect,
                              consumedFlag, blockSelect, results);
                    chan2Qsize[pChan]--;
                }
            }
            if (!hasBuffer) {
                if (get<2>(selectInstances.at(i).second)) {
                    doEmulate(selectInstances, chan2Qsize, consumedSelect,
                              consumedFlag, blockSelect, results);
                } else {
                    for (const CaseInstance &c : get<1>(selectInstances.at(i).second)) {
                        Channel::Chan *pChan = get<2>(c);
                        Channel::METHOD method = get<1>(c);
                        blockSelect[make_pair(pChan, method)].insert(i);
                    }
                    doEmulate(selectInstances, chan2Qsize, consumedSelect,
                              consumedFlag, blockSelect, results);

                    for (const CaseInstance &c : get<1>(selectInstances.at(i).second)) {
                        Channel::Chan *pChan = get<2>(c);
                        Channel::METHOD method = get<1>(c);
                        blockSelect[make_pair(pChan, method)].erase(i);
                    }
                }
            }

        }


        consumedSelect.pop_back();
        consumedFlag[i] = false;
    }
}

set<Channel::NamedStatus> emulate(const TestCase &selectInstances,
                                  std::vector<Channel::Chan *>& chanVec) {
    map<Channel::Chan *, int> chan2Qsize;
    std::for_each(chanVec.begin(), chanVec.end(),
    [&chan2Qsize](auto &a) {
        chan2Qsize[a] = 0;
    });
    std::vector<int> consumedSelect;
    std::vector<bool> consumedFlag(selectInstances.size());
    map<pair<Channel::Chan *, Channel::METHOD>, set<int>> blockSelect;
    set<Channel::NamedStatus> results;

    doEmulate(selectInstances, chan2Qsize, consumedSelect, consumedFlag,
              blockSelect, results);
    return results;
}

void printTestCase(TestCase& testCase) {
    for (auto& [selectSleepTime, selectInstance] : testCase) {
        bool isDefault = get<2>(selectInstance);
        Channel::NamedStatus namedStatus;
        string selectName = get<0>(selectInstance);
        printf("======================================\n");
        printf("default:%s\n", isDefault ? "yes" : "no");
        for (auto &[_, method, pChan, __] : get<1>(selectInstance)) {
            printf("---%s\t%s\t%s---\n", selectName.c_str(),
                   method == Channel::METHOD::READ ? "read" : "write", pChan->getName().c_str());
        }
        printf("======================================\n");
    }
}

auto taskFun = [](const microseconds &sleepTime, Channel::METHOD method) -> Channel::Task {
    auto retFun = [=](const std::string &selectName, const std::string &chanName,
    const any& a) {
        this_thread::sleep_for(sleepTime);
        LOG("###%s:%s:%s:%d\n", selectName.c_str(), ((method == Channel::METHOD::READ) ? "read" : "write"), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    };
    return retFun;
};

Channel::NamedStatus executeTestCase(const vector<Channel::Chan *> &chanVec, const TestCase& testCase) {
    vector<unique_ptr<thread>> threadPool;
    sleepTimeSum = std::chrono::microseconds{}; //reset sleep time sum
    for (auto &[selectSleepTime, selectInstance] : testCase) {
        std::vector<Channel::Case> caseVec;
        string selectName = get<0>(selectInstance);
        for (const tuple<microseconds, ::Channel::METHOD, Channel::Chan *, shared_ptr<int>> &caseTup : get<1>(selectInstance)) {
            microseconds caseSleepTime = get<0>(caseTup);
            Channel::METHOD method = get<1>(caseTup);
            caseVec.emplace_back(Channel::Command{get<2>(caseTup), method, get<3>(caseTup)}, taskFun(caseSleepTime, method));
        }
        if (get<2>(selectInstance)) {
            caseVec.emplace_back(Channel::Default{});
        }
        auto threadFun = [](string selectName, std::vector<Channel::Case> caseVec, microseconds selectSleepTime) {
            this_thread::sleep_for(selectSleepTime);
            printf("%s start\n", selectName.c_str());
            Channel::Select(selectName, caseVec.begin(), caseVec.end());
        };
        threadPool.emplace_back(new thread(threadFun, selectName, caseVec, selectSleepTime));
    }
    this_thread::sleep_for(sleepTimeSum + 1s); //make sure thread has reached a stable state
    Channel::NamedStatus namedStatus = Channel::watchNamedStatus(chanVec);
    for (auto &t : threadPool) {
        t->detach();
    }
    return namedStatus;
}

void testcase(std::mt19937& engine) {
    std::vector<ChanPtr> chans = sampleChan(engine);
    std::vector<Channel::Chan *> chanVec;
    transform(chans.begin(), chans.end(), std::back_inserter(chanVec), [](auto &c) {
        return c.get();
    });
    printChannel(chanVec);

    TestCase testCase = sampleTestCase(engine, chanVec);
    printTestCase(testCase);

    set<Channel::NamedStatus> emulateResult = emulate(testCase, chanVec);
    std::cout << "Select num:" << testCase.size()
              << " result size:" << emulateResult.size() << std::endl;
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
int main(int argc, char** args) {
    int seed = stoi(args[1]);
    std::cout << "seed:" << seed << std::endl;
    std::mt19937 engine(seed);
    testcase(engine);
    return 0;
}