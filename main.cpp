#include <channel.h>

Channel::Chan chan1{"chan1"}, chan2{"chan2"};
std::vector<Channel::Chan*> chanVec{&chan1, &chan2};

Channel::Task taskWrite = [](const std::string& selectName, const std::string& chanName,
int a) {
    log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
    return true;
};

Channel::Task taskRead = [](const std::string& selectName, const std::string& chanName,
int a) {
    log("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
    return true;
};

void fun() {
    std::this_thread::sleep_for(0s);
    int a = 10;

    chan1.write(&a,
    [](const std::string& selectName, const std::string& chanName, int a) {
        log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
        return true;
    }
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