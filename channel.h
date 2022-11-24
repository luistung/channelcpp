#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <any>
#include <queue>

using namespace std::chrono_literals;

#ifdef NDEBUG
#define LOG(...) void(0)
#else
#define LOG printf
#endif

namespace Channel {

class Select;
class Chan;

enum METHOD { READ, WRITE };

using Task = std::function<bool(const std::string &, const std::string &, const std::any&)>;
using Status = std::vector<std::tuple<Select *, METHOD, Chan *>>;
using NamedStatus = std::set<std::tuple<std::string, METHOD, std::string>>;

struct Command {
    Channel::Chan *pChan;
    METHOD method;
    std::any pVal;
};

class Case {
  public:
    Case() = default;
    Case(const Case &case_) = default;
    Case(Command&& command, Task pFunc) : mMethod(command.method),
        mpChan(command.pChan),
        mpVal(command.pVal),
        mpFunc(pFunc) {}

  private:
    friend class Select;
    void exec(const Select *pSelect);
    bool tryExec(const Select *pSelect);
    METHOD mMethod;
    Chan *mpChan = nullptr;
    std::any mpVal;
    Task mpFunc;
};
using Default = Case;

class Select {
  public:
    Select(std::initializer_list<Case> caseVec);
    template <typename... T> Select(const std::string &name, T... caseVec);
    template <
        typename T,
        typename std::enable_if<
            std::is_same_v<typename std::iterator_traits<T>::value_type, Case>,
            void>::type * = nullptr>
    Select(const std::string &name, T begin, T end);

  private:
    template <typename T> void doSelect(const std::string &name, T begin, T end);
    void doSelect(const std::string &name, std::initializer_list<Case> caseVec);
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

Command operator>>(Chan*pChan, std::any pVal) {
    return Command{pChan, METHOD::READ, pVal};
}

Command operator<<(Chan*pChan, std::any pVal) {
    return Command{pChan, METHOD::WRITE, pVal};
}

class Chan {
  public:
    Chan(const std::string &name = "") : mName(name) {};
    Chan(int capacity, const std::string &name = "") : mCapacity(capacity), mName(name) {};


    void doWrite(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);

        mPayload = val;
        mCv.notify_one();
    }

    void doRead(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCv.wait(lock, [&] { return mPayload.has_value(); });
        val.swap(mPayload);
        mPayload.reset();

    }

    bool tryWrite(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (full()) {
            return false;
        }
        mBuffer.emplace(val);
        return true;
    }

    bool tryRead(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (empty()) {
            return false;
        }
        val.swap(mBuffer.front());
        mBuffer.pop();

        return true;
    }

    void bufferPush() {
        mBuffer.push(mPayload);
        mPayload.reset();
    }

    void bufferPop() {
        mPayload = mBuffer.front();
        mBuffer.pop();
    }

    bool empty() const {
        return mBuffer.size() == 0;
    }
    bool full() const {
        return mBuffer.size() >= mCapacity;
    }
    bool isBuffered() const {
        return mCapacity > 0;
    }

    void write(std::any val, Task fun) {
        Select{mName, Case{this << val, fun}};
    }

    void read(std::any val, Task fun) {
        Select{mName, Case{this >> val, fun}};
    }

    std::string getName() const {
        return mName;
    }

    size_t getCapacity() const {
        return mCapacity;
    }

  private:
    friend class Case;
    friend class Select;
    friend Status watchStatus(const std::vector<Chan *> &chanVec);
    friend NamedStatus watchNamedStatus(const std::vector<Chan *> &chanVec);
    friend void printStatus(const Status &status);
    std::string mName;

    std::queue<std::any> mBuffer{};
    int mCapacity{0};
    std::any mPayload;

    std::mutex mMutex; // protect mBuffer
    std::condition_variable mCv;

    std::list<std::pair<Select *, METHOD>> waitingSelectList;
};

struct Coordinator {
    std::mutex mMutex;
};
Coordinator gCoordinator;

void Case::exec(const Select *pSelect) {
    if (mMethod == READ) {
        mpChan->doRead(pSelect, mpVal);
    } else {
        mpChan->doWrite(pSelect, mpVal);
    }
    mpFunc(pSelect->mName, mpChan->mName, mpVal);
}

bool Case::tryExec(const Select *pSelect) {
    //bool flag = (mMethod == READ) ? mpChan->tryRead(pSelect, mpVal)
    //                              : mpChan->tryWrite(pSelect, mpVal);
    bool flag = false;
    if (mMethod == READ) {
        flag = mpChan->tryRead(pSelect, mpVal);
    } else
        flag = mpChan->tryWrite(pSelect, mpVal);
    if (!flag) return false;
    mpFunc(pSelect->mName, mpChan->mName, mpVal);
    return true;
}

template <typename... T> Select::Select(const std::string &name, T... caseVec) {
    doSelect(name, {caseVec...});
}

Select::Select(std::initializer_list<Case> caseVec) {
    doSelect("", caseVec.begin(), caseVec.end());
}

template <
    typename T,
    typename std::enable_if<
        std::is_same_v<typename std::iterator_traits<T>::value_type, Case>,
        void>::type *>
Select::Select(const std::string &name, T begin, T end) {
    doSelect(name, begin, end);
}

void Select::doSelect(const std::string &name,
                      std::initializer_list<Case> caseVec) {
    doSelect(name, caseVec.begin(), caseVec.end());
}

template <typename T>
void Select::doSelect(const std::string &name, T begin, T end) {
    this->mName = name;
    bool hasDefault = false;
    for (auto it = begin; it != end; it++) {
        auto &case_ = *it;
        if (case_.mpChan==nullptr) {
            if (it != end -1) throw std::runtime_error("default must be at the end");
            hasDefault = true;
            continue;
        }
        if (mpChan2Case.find(case_.mpChan) != mpChan2Case.end()) {
            throw std::runtime_error("duplicated chan in same select");
        }
        mpChan2Case[case_.mpChan] = case_;
    }

    Select *pSelect = nullptr;
    Case *pCase = nullptr;
    bool hasWaiter = false;
    {
        std::unique_lock<std::mutex> gLock(gCoordinator.mMutex);
        for (auto &pChan2CasePair : mpChan2Case) {
            pCase = &pChan2CasePair.second;
            Chan *pChan = pCase->mpChan;


            if (pCase->mMethod == READ) {
                if (!pChan->waitingSelectList.empty() &&
                        pChan->waitingSelectList.back().second == WRITE) {
                    pSelect = pChan->waitingSelectList.back()
                              .first; // remove blocked select
                    pChan->waitingSelectList.pop_back();
                    LOG("%s removed from %s's waiting list by %s\n", pSelect->mName.c_str(), pChan->mName.c_str(), this->mName.c_str());
                    hasWaiter = true;
                    break;
                }
            } else if (pCase->mMethod == WRITE) {
                if (!pChan->waitingSelectList.empty() &&
                        pChan->waitingSelectList.front().second == READ) {
                    pSelect = pChan->waitingSelectList.front().first;
                    pChan->waitingSelectList.pop_front();
                    LOG("%s removed from %s's waiting list by %s\n", pSelect->mName.c_str(), pChan->mName.c_str(), this->mName.c_str());
                    hasWaiter = true;
                    break;
                }
            }

        } // for


        if (hasWaiter) {
            // de-register peer

            for (auto &pChan2CasePair : pSelect->mpChan2Case) {
                Chan *pChan = pChan2CasePair.first;
                pChan->waitingSelectList.remove_if(
                [=](std::pair<Select *, METHOD> &a) {
                    LOG("%s removed from %s's waiting list\n", pSelect->mName.c_str(), pChan->mName.c_str());
                    return a.first == pSelect;
                });
            }
        }


        if (!hasWaiter) {

            for (auto &pChan2CasePair : mpChan2Case) {
                pCase = &pChan2CasePair.second;
                Chan *pChan = pCase->mpChan;
                if (pChan->isBuffered()) {
                    if (pCase->tryExec(this)) {
                        LOG("%s non block\n", this->mName.c_str());
                        return;
                    }
                }
            }
            //not have buffered data
            if (hasDefault) return;
            // register self
            for (auto &pChan2CasePair : mpChan2Case) {
                auto &case_ = pChan2CasePair.second;
                LOG("%s add into %s's waiting list\n", this->mName.c_str(), case_.mpChan->mName.c_str());
                if (case_.mMethod == READ) {
                    case_.mpChan->waitingSelectList.emplace_front(this, READ);
                } else {
                    case_.mpChan->waitingSelectList.emplace_back(this, WRITE);
                }
            }
        }

    } // gLock
    if (hasWaiter) {
        {
            std::unique_lock<std::mutex> lock(pSelect->mMutex);
            LOG("%s notify %s \n", this->mName.c_str(), pSelect->mName.c_str());
            pSelect->mpChanTobeNotified = pCase->mpChan;
        }
        pSelect->mCv.notify_one();
        pCase->exec(this);
        return;
    }



    std::unique_lock<std::mutex> lock(mMutex);
    mCv.wait(lock, [=]() {
        return mpChanTobeNotified != nullptr;
    });
    LOG("%s notified\n", this->mName.c_str());

    mpChan2Case[mpChanTobeNotified].exec(this);
}

Status watchStatus(const std::vector<Chan *> &chanVec) {
    std::lock_guard<std::mutex>(gCoordinator.mMutex);
    Status ret;
    for (auto &pChan : chanVec) {
        for (auto &[pSelect, method] : pChan->waitingSelectList) {
            ret.emplace_back(pSelect, method, pChan);
        }
    }
    return ret;
}

NamedStatus watchNamedStatus(const std::vector<Chan *> &chanVec) {
    std::lock_guard<std::mutex>(gCoordinator.mMutex);
    NamedStatus ret;
    for (auto &pChan : chanVec) {
        for (auto &[pSelect, method] : pChan->waitingSelectList) {
            LOG("%s found in %s's waiting list\n", pSelect->mName.c_str(),
                pChan->mName.c_str());
            ret.insert(make_tuple(pSelect->mName, method, pChan->mName));
        }
    }
    return ret;
}

void printStatus(const Status &status) {
    printf("======================================\n");
    for (auto &[pSelect, method, pChan] : status) {
        printf("---%s\t%s\t%s---\n", pSelect->mName.c_str(),
               method == METHOD::READ ? "read" : "write", pChan->mName.c_str());
    }
    printf("======================================\n");
}

void printNamedStatus(const NamedStatus &status) {
    printf("======================================\n");
    for (auto &[selectName, method, chanName] : status) {
        printf("---%s\t%s\t%s---\n", selectName.c_str(),
               method == METHOD::READ ? "read" : "write", chanName.c_str());
    }
    printf("======================================\n");
}

void printChannel(const std::vector<Channel::Chan *>& chanVec) {
    printf("======================================\n");
    for (Channel::Chan *chan : chanVec) {
        printf("---%s\t%d---\n", chan->getName().c_str(), static_cast<int>(chan->getCapacity()));
    }
    printf("======================================\n");
}

} // namespace Channel
