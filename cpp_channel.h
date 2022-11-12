#include <map>
#include <deque>
#include <list>
#include <vector>
#include <future>
#include <functional>
#include <iostream>
using namespace std::chrono_literals;

template<typename... T>
void log(T... args) {
    printf(args...);
}

namespace Chan {

class Switch;
class Chan;

enum METHOD
{
    READ,
    WRITE
};

using Task = std::function<bool(const std::string&, const std::string&, int)>;
using Status = std::vector<std::tuple<Switch*, METHOD, Chan*>>;
using NamedStatus = std::vector<std::tuple<std::string, METHOD, std::string>>;

class Case
{
public:
    Case() = default;
    Case(const Case& case_) = default;
    Case(METHOD method, Chan* pChan, int* pVal, Task pFunc) :
        mMethod(method), mpChan(pChan), mpVal(pVal), mpFunc(pFunc) {}

private:
    friend class Switch;
    void exec(const Switch* pSwitch);
    METHOD mMethod;
    Chan *mpChan;
    int *mpVal;
    Task mpFunc;
};

class Switch
{
public:    
    Switch(std::initializer_list<Case> caseVec);
    template <typename ...T>
    Switch(const std::string& name, T... caseVec);

private:
    void doSwitch(std::initializer_list<Case> caseVec);
    friend class Case;
    friend Status watchStatus(const std::vector<Chan *> &chanVec);
    friend NamedStatus watchNamedStatus(const std::vector<Chan *> &chanVec);
    friend void printStatus(const Status &status);

    std::string mName;
    std::map<Chan *, Case> mpChan2Case;
    std::mutex mMutex;
    std::condition_variable mCv;
    Chan *mpChanTobeNotified{nullptr};
};

class Chan {
public:
    Chan(const std::string& name = "") : mName(name) {};

    void doWrite(const Switch* pSwitch, int *val)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mBuffer.push_back(*val);
        mCv.notify_one();
    }

    int doRead(const Switch* pSwitch) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCv.wait(lock, [&]
                 { return mBuffer.size() > 0; });
        int val = mBuffer.back();
        mBuffer.pop_back();
        return val;
    }

    void write(int *val, Task fun) {
        Switch {
            mName,
            Case{
                METHOD::WRITE,
                this,
                val,
                fun
            }
        };
    }

    void read(int *val, Task fun) {
        Switch{
            mName,
            Case{
                METHOD::READ,
                this,
                val,
                fun}};
    }

    

private:
    friend class Case;
    friend Status watchStatus(const std::vector<Chan *> &chanVec);
    friend NamedStatus watchNamedStatus(const std::vector<Chan *> &chanVec);
    friend void printStatus(const Status &status);
    std::string mName;

    std::vector<int> mBuffer;

    std::mutex mMutex;  // protect mBuffer
    std::condition_variable mCv;

public:
    std::list<std::pair<Switch *, METHOD>> waitingSwitchList;
};

struct Coordinator
{
    std::mutex mMutex;
    std::condition_variable mCv;
};
Coordinator gCoordinator;



void Case::exec(const Switch* pSwitch) {
    if (mMethod == READ)
    {
        *mpVal = mpChan->doRead(pSwitch);
    }
    else {
        mpChan->doWrite(pSwitch, mpVal);
    }
    mpFunc(pSwitch->mName, mpChan->mName, *mpVal);
}

template <typename ...T>
Switch::Switch(const std::string& name, T... caseVec) {
    this->mName = name;
    doSwitch({caseVec...});
}

Switch::Switch(std::initializer_list<Case> caseVec) {
    doSwitch(caseVec);
}

void Switch::doSwitch(std::initializer_list<Case> caseVec)  {
    for (auto &case_ : caseVec)
    {
        if (mpChan2Case.find(case_.mpChan) != mpChan2Case.end())
            throw std::runtime_error("duplicated chan in same switch");
        mpChan2Case[case_.mpChan] = case_;
    }

    Switch *pSwitch = nullptr;
    Case *pCase = nullptr;
    bool hasWaiter = false;
    {
        std::unique_lock<std::mutex> gLock(gCoordinator.mMutex);
        for (auto& pChan2CasePair : mpChan2Case) {
            
            pCase = &pChan2CasePair.second;
            
            if (pCase->mMethod == READ)
            { 
                if (!pCase->mpChan->waitingSwitchList.empty() && pCase->mpChan->waitingSwitchList.back().second == WRITE) {
                    pSwitch = pCase->mpChan->waitingSwitchList.back().first; // remove blocked switch
                    pCase->mpChan->waitingSwitchList.pop_back();
                    hasWaiter = true;
                    break;
                }
            }
            else if (pCase->mMethod == WRITE) {               
                if (!pCase->mpChan->waitingSwitchList.empty() && pCase->mpChan->waitingSwitchList.front().second == READ) {
                    pSwitch = pCase->mpChan->waitingSwitchList.front().first;
                    pCase->mpChan->waitingSwitchList.pop_front();
                    hasWaiter = true;
                    break;
                }
            }
            
        } //for
        
        if (hasWaiter)
        {
            //de-register
            for (auto& pChan2CasePair : mpChan2Case) {
                if (pChan2CasePair.first == pCase->mpChan)
                    continue;
                
                auto &case_ = pChan2CasePair.second;
                case_.mpChan->waitingSwitchList.remove_if([=](auto &a)
                                                                        { return a.first == this;});
            }
        }

    } //gLock
    if (hasWaiter) {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            pSwitch->mpChanTobeNotified = pCase->mpChan;
            pSwitch->mCv.notify_one();
        }
        pCase->exec(this);
        return;
    }

    //register
    {
        std::unique_lock<std::mutex> gLock(gCoordinator.mMutex);
        for (auto& pChan2CasePair : mpChan2Case) {
            
            auto &case_ = pChan2CasePair.second;
            if (case_.mMethod == READ) {
                case_.mpChan->waitingSwitchList.emplace_front(this, READ);
            }
            else {
                case_.mpChan->waitingSwitchList.emplace_back(this, WRITE);
            }
        }
    } //gLock
    
    //std::cout << std::this_thread::get_id() << ":###" << std::endl;
    std::unique_lock<std::mutex> lock(mMutex);
    mCv.wait(lock, [=]()
                { 
                //std::cout << std::this_thread::get_id() << ":" << mpChanTobeNotified << std::endl;
                return mpChanTobeNotified != nullptr; });
    //std::cout << std::this_thread::get_id() << ":###" << std::endl;
    
    mpChan2Case[mpChanTobeNotified].exec(this);

}

Status watchStatus(const std::vector<Chan*>& chanVec) {
    std::lock_guard<std::mutex>(gCoordinator.mMutex);
    Status ret;
    for (auto &pChan : chanVec)
    {
        for (auto& [pSwitch, method] : pChan->waitingSwitchList) {
            ret.emplace_back(pSwitch, method, pChan);
        }
    }
    return ret;
}

NamedStatus watchNamedStatus(const std::vector<Chan*>& chanVec) {
    std::lock_guard<std::mutex>(gCoordinator.mMutex);
    NamedStatus ret;
    for (auto &pChan : chanVec)
    {
        for (auto& [pSwitch, method] : pChan->waitingSwitchList) {
            ret.emplace_back(pSwitch->mName, method, pChan->mName);
        }
    }
    return ret;
}

void printStatus(const Status& status) {
    printf("======================================\n");
    for (auto &[pSwitch, method, pChan] : status)
    {
        printf("---%s\t%s\t%s---\n", pSwitch->mName.c_str(), method == METHOD::READ ? "read" : "write", pChan->mName.c_str());
    }
    printf("======================================\n");
}
} //ns

