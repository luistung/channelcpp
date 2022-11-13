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

namespace Channel {

class Select;
class Chan;

enum METHOD
{
    READ,
    WRITE
};

using Task = std::function<bool(const std::string&, const std::string&, int)>;
using Status = std::vector<std::tuple<Select*, METHOD, Chan*>>;
using NamedStatus = std::vector<std::tuple<std::string, METHOD, std::string>>;

class Case
{
public:
    Case() = default;
    Case(const Case& case_) = default;
    Case(METHOD method, Chan* pChan, int* pVal, Task pFunc) :
        mMethod(method), mpChan(pChan), mpVal(pVal), mpFunc(pFunc) {}

private:
    friend class Select;
    void exec(const Select* pSelect);
    METHOD mMethod;
    Chan *mpChan;
    int *mpVal;
    Task mpFunc;
};

class Select
{
public:    
    Select(std::initializer_list<Case> caseVec);
    template <typename ...T>
    Select(const std::string& name, T... caseVec);

private:
    void doSelect(std::initializer_list<Case> caseVec);
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

    void doWrite(const Select* pSelect, int *val)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mBuffer.push_back(*val);
        mCv.notify_one();
    }

    int doRead(const Select* pSelect) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCv.wait(lock, [&]
                 { return mBuffer.size() > 0; });
        int val = mBuffer.back();
        mBuffer.pop_back();
        return val;
    }

    void write(int *val, Task fun) {
        Select {
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
        Select{
            mName,
            Case{
                METHOD::READ,
                this,
                val,
                fun}};
    }

    std::string getName() {
        return mName;
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
    std::list<std::pair<Select *, METHOD>> waitingSelectList;
};

struct Coordinator
{
    std::mutex mMutex;
    std::condition_variable mCv;
};
Coordinator gCoordinator;



void Case::exec(const Select* pSelect) {
    if (mMethod == READ)
    {
        *mpVal = mpChan->doRead(pSelect);
    }
    else {
        mpChan->doWrite(pSelect, mpVal);
    }
    mpFunc(pSelect->mName, mpChan->mName, *mpVal);
}

template <typename ...T>
Select::Select(const std::string& name, T... caseVec) {
    this->mName = name;
    doSelect({caseVec...});
}

Select::Select(std::initializer_list<Case> caseVec) {
    doSelect(caseVec);
}

void Select::doSelect(std::initializer_list<Case> caseVec)  {
    for (auto &case_ : caseVec)
    {
        if (mpChan2Case.find(case_.mpChan) != mpChan2Case.end())
            throw std::runtime_error("duplicated chan in same select");
        mpChan2Case[case_.mpChan] = case_;
    }

    Select *pSelect = nullptr;
    Case *pCase = nullptr;
    bool hasWaiter = false;
    {
        std::unique_lock<std::mutex> gLock(gCoordinator.mMutex);
        for (auto& pChan2CasePair : mpChan2Case) {
            
            pCase = &pChan2CasePair.second;
            
            if (pCase->mMethod == READ)
            { 
                if (!pCase->mpChan->waitingSelectList.empty() && pCase->mpChan->waitingSelectList.back().second == WRITE) {
                    pSelect = pCase->mpChan->waitingSelectList.back().first; // remove blocked select
                    pCase->mpChan->waitingSelectList.pop_back();
                    hasWaiter = true;
                    break;
                }
            }
            else if (pCase->mMethod == WRITE) {               
                if (!pCase->mpChan->waitingSelectList.empty() && pCase->mpChan->waitingSelectList.front().second == READ) {
                    pSelect = pCase->mpChan->waitingSelectList.front().first;
                    pCase->mpChan->waitingSelectList.pop_front();
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
                case_.mpChan->waitingSelectList.remove_if([=](auto &a)
                                                                        { return a.first == this;});
            }
        }

    } //gLock
    if (hasWaiter) {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            pSelect->mpChanTobeNotified = pCase->mpChan;
            pSelect->mCv.notify_one();
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
                case_.mpChan->waitingSelectList.emplace_front(this, READ);
            }
            else {
                case_.mpChan->waitingSelectList.emplace_back(this, WRITE);
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
        for (auto& [pSelect, method] : pChan->waitingSelectList) {
            ret.emplace_back(pSelect, method, pChan);
        }
    }
    return ret;
}

NamedStatus watchNamedStatus(const std::vector<Chan*>& chanVec) {
    std::lock_guard<std::mutex>(gCoordinator.mMutex);
    NamedStatus ret;
    for (auto &pChan : chanVec)
    {
        for (auto& [pSelect, method] : pChan->waitingSelectList) {
            ret.emplace_back(pSelect->mName, method, pChan->mName);
        }
    }
    return ret;
}

void printStatus(const Status& status) {
    printf("======================================\n");
    for (auto &[pSelect, method, pChan] : status)
    {
        printf("---%s\t%s\t%s---\n", pSelect->mName.c_str(), method == METHOD::READ ? "read" : "write", pChan->mName.c_str());
    }
    printf("======================================\n");
}

void printNamedStatus(const NamedStatus& status) {
    printf("======================================\n");
    for (auto &[selectName, method, chanName] : status)
    {
        printf("---%s\t%s\t%s---\n", selectName.c_str(), method == METHOD::READ ? "read" : "write", chanName.c_str());
    }
    printf("======================================\n");
}

} //ns

