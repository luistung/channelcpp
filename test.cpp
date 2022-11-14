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
    for (Channel::Chan *pChan : chanVec)
    {
        if (!sampleBernoulli(engine))
            continue;
        Channel::METHOD method = sampleBernoulli(engine) ? ::Channel::METHOD::WRITE : ::Channel::METHOD::READ;
        ret.second.push_back(make_tuple(sampleSleep(engine), method, pChan, make_shared<int>(0)));
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

void doEmulate(const TestCase& selectInstances, 
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
        for (auto& [_, method, pChan, __] : pSelectIns->second) {
            wait2selectIns[make_pair(method, pChan)].push_back(pSelectIns);
        }
    }

    const SelectInstance* pSelectInstance = &selectInstances[pos].second;
    bool found = false;
    for (auto &[_, method, pChan, __] : pSelectInstance->second)
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

set<Channel::NamedStatus> emulate(const TestCase& selectInstances) {
    set<const SelectInstance *> cur;
    set<set<const SelectInstance *>> res;
    doEmulate(selectInstances, 0, cur, res);

    set<Channel::NamedStatus> ret;
    for (const set<const SelectInstance *> &selectInstanceSet : res) {
        Channel::NamedStatus status;
        for (const SelectInstance *pSelectInstance : selectInstanceSet) {
            string selectName = pSelectInstance->first;
            for (auto &[_, method, pChan, __] : pSelectInstance->second) {
                status.insert(make_tuple(selectName, method, pChan->getName()));
            }
        }
        ret.insert(status);
    }
    return ret;
}

vector<Channel::NamedStatus> getInitNameStatus(TestCase& selectInstances) {
    vector<Channel::NamedStatus> ret;
    for (auto& [selectSleepTime, selectInstance] : selectInstances) {
        Channel::NamedStatus namedStatus;
        string selectName = selectInstance.first;
        for (auto &[_, method, pChan, __] : selectInstance.second)
        {
            namedStatus.insert(make_tuple(selectName, method, pChan->getName()));
        }
        ret.emplace_back(namedStatus);
    }
    return ret;
}

auto taskFun = [](const microseconds &sleepTime, Channel::METHOD method) -> Channel::Task
{
    auto retFun = [=](const std::string &selectName, const std::string &chanName,
                     int a)
    {
        //this_thread::sleep_for(sleepTime);
        log("###%s:%s:%s:%d\n", selectName.c_str(), ((method == Channel::METHOD::READ) ? "read" : "write"), chanName.c_str(), a);
        return true;
    };
    return retFun;
};

Channel::NamedStatus executeTestCase(const vector<Channel::Chan *> &chanVec, const TestCase& testCase) {
    vector<thread> threadPool;
    for (auto& [selectSleepTime, selectInstance] : testCase)
    {
        std::vector<Channel::Case> caseVec;
        string selectName = selectInstance.first;
        for (const tuple<microseconds, ::Channel::METHOD, Channel::Chan *, shared_ptr<int>> &caseTup : selectInstance.second)
        {
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
    transform(chans.begin(), chans.end(), std::back_inserter(chanVec), [](auto &c)
              { return c.get(); });
    TestCase testCase = sampleTestCase(engine, chanVec);
    for(auto &i : getInitNameStatus(testCase)) {
        printNamedStatus(i);
    }

    set<Channel::NamedStatus> emulateResult = emulate(testCase);
    std::cout << "Select num:" << testCase.size() << " result size:" << emulateResult.size() << std::endl;
    cout << "expected results:" << endl;
    for (auto &i : emulateResult)
    {
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
        std::mt19937 engine(i);
        testcase(engine);
    }
    return 0;
}