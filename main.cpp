#include <channel.h>
using namespace std;

Channel::Chan chan1{"chan1"}, chan2{"chan2"};
std::vector<Channel::Chan*> chanVec{&chan1, &chan2};

Channel::Task taskWrite = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<int*>(a));
    return true;
};

Channel::Task taskRead = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    log("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<int*>(a));
    return true;
};

void fun() {
    std::this_thread::sleep_for(0s);
    int a = 10;

    chan1.write(&a,
    [](const std::string& selectName, const std::string& chanName, const std::any& a) {
        log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<int*>(a));
        return true;
    }
               );

}
void fun2() {
    int a=20, b=30;
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
}


int testNonBuffered() {

    std::thread t([&]() {
        fun();
    });
    std::thread t2([&]() {
        fun2();
    });
    std::this_thread::sleep_for(1s);
    printStatus(Channel::watchStatus(chanVec));
    t.join();
    t2.join();

    return 0;
}

Channel::Chan bchan1{1, "bchan1"}, bchan2{1, "bchan2"};
std::vector<Channel::Chan*> chanVec2{&bchan1, &bchan2};
void fun3() {
    std::this_thread::sleep_for(0s);
    shared_ptr<int> a = make_shared<int>(10), b = make_shared<int>(20);

    bchan1.write(a,
    [](const std::string& selectName, const std::string& chanName, const std::any& a) {
        log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    }
                );
    bchan1.write(b, [](const std::string& selectName,
    const std::string& chanName, const std::any& a) {
        log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    });
}

void fun4() {
    shared_ptr<int> a;
    bchan1.read(a, [](const std::string& selectName,
                       const std::string& chanName, const std::any& a) {
        //log("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<int*>(a));
        log("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));

        return true;
    });
}

int testBuffered() {
    std::thread t([&]() {
        std::this_thread::sleep_for(1s);
        fun3(); //write
    });
    
    std::thread t2([&]() {
        
        fun4(); //read
    });

    t2.join();
    t.join();
    return 0;
}

int main() {
    testNonBuffered();
    testBuffered();
    return 0;
}