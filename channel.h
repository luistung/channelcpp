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

template <typename... T> void log(T... args) {
    printf(args...);
}

namespace Channel {

class Select;
class Chan;

enum METHOD { READ, WRITE };

using Task = std::function<bool(const std::string &, const std::string &, const std::any&)>;
using Status = std::vector<std::tuple<Select *, METHOD, Chan *>>;
using NamedStatus = std::set<std::tuple<std::string, METHOD, std::string>>;

class Case {
  public:
    Case() = default;
    Case(const Case &case_) = default;
    Case(METHOD method, Chan *pChan, std::any pVal, Task pFunc)
        : mMethod(method), mpChan(pChan), mpVal(pVal), mpFunc(pFunc) {}

  private:
    friend class Select;
    void exec(const Select *pSelect);
    bool tryExec(const Select *pSelect);
    METHOD mMethod;
    Chan *mpChan;
    std::any mpVal;
    Task mpFunc;
};

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

class Chan {
  public:
    Chan(const std::string &name = "") : mName(name) {};
    Chan(int size, const std::string &name = "") : mSize(size), mName(name) {};


    void doWrite(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);

        mPayload = val;
        if (isBuffered()) bufferPush();
        mCv.notify_all();
    }

    void doRead(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCv.wait(lock, [&] { return isBuffered() ? !empty() : mPayload.has_value(); });
        if (isBuffered()) bufferPop();
        val.swap(mPayload);
        mPayload.reset();

    }

    bool tryWrite(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (full()) {
            return false;
        }
        mPayload = val;
        if (isBuffered()) bufferPush();
        return true;
    }

    bool tryRead(const Select *pSelect, std::any& val) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (empty()) {
            return false;
        }
        if (isBuffered()) bufferPop();
        val.swap(mPayload);
        mPayload.reset();
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

    bool empty() {
        return mBuffer.size() == 0;
    }
    bool full() {
        return mBuffer.size() >= mSize;
    }
    bool isBuffered() {
        return mSize > 0;
    }

    void write(int *val, Task fun) {
        Select{mName, Case{METHOD::WRITE, this, val, fun}};
    }

    void read(int *val, Task fun) {
        Select{mName, Case{METHOD::READ, this, val, fun}};
    }

    std::string getName() {
        return mName;
    }

  private:
    friend class Case;
    friend class Select;
    friend Status watchStatus(const std::vector<Chan *> &chanVec);
    friend NamedStatus watchNamedStatus(const std::vector<Chan *> &chanVec);
    friend void printStatus(const Status &status);
    std::string mName;

    std::queue<std::any> mBuffer{};
    int mSize{0};
    std::any mPayload;

    std::mutex mMutex; // protect mBuffer
    std::condition_variable mCv;

  public:
    std::list<std::pair<Select *, METHOD>> waitingSelectList;
};

struct Coordinator {
    std::mutex mMutex;
    std::condition_variable mCv;
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
    for (auto it = begin; it != end; it++) {
        auto &case_ = *it;
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
                    hasWaiter = true;
                    break;
                }
            } else if (pCase->mMethod == WRITE) {
                if (!pChan->waitingSelectList.empty() &&
                        pChan->waitingSelectList.front().second == READ) {
                    pSelect = pChan->waitingSelectList.front().first;
                    pChan->waitingSelectList.pop_front();
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
                        return;
                    }
                }
            }
            // register self
            for (auto &pChan2CasePair : mpChan2Case) {
                auto &case_ = pChan2CasePair.second;
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
            std::unique_lock<std::mutex> lock(mMutex);
            pSelect->mpChanTobeNotified = pCase->mpChan;
        }
        pCase->exec(this);
        pSelect->mCv.notify_all();
        return;
    }



    // std::cout << std::this_thread::get_id() << ":###" << std::endl;
    std::unique_lock<std::mutex> lock(mMutex);
    mCv.wait(lock, [=]() {
        // std::cout << std::this_thread::get_id() << ":" << mpChanTobeNotified <<
        // std::endl;
        return mpChanTobeNotified != nullptr;
    });
    // std::cout << std::this_thread::get_id() << ":###" << std::endl;

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

} // namespace Channel
