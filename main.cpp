#include <channel.h>

Chan::Chan chan1{"chan1"}, chan2{"chan2"};
std::vector<Chan::Chan*> chanVec{&chan1, &chan2};

Chan::Task taskWrite = [](const std::string& selectName, const std::string& chanName,
                    int a) {
                    log("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), a);
                    return true;
};

Chan::Task taskRead = [](const std::string& selectName, const std::string& chanName,
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
    std::thread t([&]()
           { fun(); });
    
    std::this_thread::sleep_for(0s);
    Chan::Select{
        "select1",
        Chan::Case{
            Chan::METHOD::READ,
            &chan1,
            &a,
            taskRead
        },
        Chan::Case{
            Chan::METHOD::WRITE,
            &chan2,
            &b,
            taskWrite
        }

    };
    std::this_thread::sleep_for(1s);
    printStatus(Chan::watchStatus(chanVec));
    t.join();

    return 0;
}