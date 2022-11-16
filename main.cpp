#include <channel.h>
using namespace std;

Channel::Chan chan1{"chan1"}, chan2{"chan2"};
std::vector<Channel::Chan*> chanVec{&chan1, &chan2};

std::function<bool(const std::string&, const std::string&, const int&)> taskWrite = [](const std::string& selectName, const std::string& chanName,
const int& a) {
    log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
    return true;
};

std::function<bool(const std::string&, const std::string&, const int&)> taskRead = [](const std::string& selectName, const std::string& chanName,
const int& a) {
    log("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
    return true;
};

void fun() {
    std::this_thread::sleep_for(0s);
    int a = 10;

    std::function<bool(const std::string&, const std::string&, const int&)>
        fun = [](const std::string& selectName, const std::string& chanName,
                 const int& a) {
            log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
            return true;
        };

    chan1.write(&a,
                fun
               );

}


int main() {
    int a, b;
    std::thread t([&]() {
        fun();
    });

    std::this_thread::sleep_for(0s);
    Channel::Select{
        "select1",
        Channel::Case{
            Channel::METHOD::READ,
            &chan1,
            &a,
            taskRead
        },
        Channel::Case{
            Channel::METHOD::WRITE,
            &chan2,
            &b,
            taskWrite
        }

    };
    std::this_thread::sleep_for(1s);
    printStatus(Channel::watchStatus(chanVec));
    t.join();

    return 0;
}