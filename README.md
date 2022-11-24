# channelcpp
c++ versione channel as in golang

# example

``` c++
#include <channel.h>
using namespace std;


Channel::Task taskWrite = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    printf("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
    return true;
};

Channel::Task taskRead = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    printf("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
    return true;
};

void fun1(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a = make_shared<int>(10);

    Channel::Select{
        Channel::Case{
            chanVec[0] << a,
            taskWrite
        }
    };
}
void fun2(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a = make_shared<int>(20), b=make_shared<int>();
    Channel::Select{
        Channel::Case{
            chanVec[0] << a,
            taskWrite
        },
        Channel::Case{
            chanVec[1] >> b,
            taskRead
        }
    };
}

void fun3(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a = make_shared<int>(30);
    Channel::Select{
        "Select3", //select name
        Channel::Case{
            chanVec[1] << a,
            taskWrite
        }
    };
}


int main() {
    Channel::Chan nonbuffer_chan{1, "chan1"}, buffer_chan{0, "chan2"};
    std::vector<Channel::Chan*> chanVec{&nonbuffer_chan, &buffer_chan};

    std::thread t1([&]() {
        fun1(chanVec);
    });
    std::thread t2([&]() {
        fun2(chanVec);
    });
    std::thread t3([&]() {
        fun3(chanVec);
    });
    t1.join();
    t2.join();
    t3.join();

    return 0;
}
```
