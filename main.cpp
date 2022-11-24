#include <channel.h>
using namespace std;


Channel::Task taskWrite = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    LOG("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
    return true;
};

Channel::Task taskRead = [](const std::string& selectName, const std::string& chanName,
const std::any& a) {
    LOG("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
    return true;
};

void fun(std::vector<Channel::Chan*>& chanVec) {
    std::this_thread::sleep_for(0s);
    shared_ptr<int> a = make_shared<int>(10);

    chanVec[0]->write(a,
    [](const std::string& selectName, const std::string& chanName, const std::any& a) {
        LOG("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    }
               );

}
void fun2(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a = make_shared<int>(20), b=make_shared<int>(30);
    Channel::Select{
        "select1",
        Channel::Case{
            chanVec[0] >> a,
            taskRead
        },
        Channel::Case{
            chanVec[1] << b,
            taskWrite
        }

    };
}


int testNonBuffered() {
    Channel::Chan chan1{"chan1"}, chan2{"chan2"};
    std::vector<Channel::Chan*> chanVec{&chan1, &chan2};

    std::thread t([&]() {
        fun(chanVec);
    });
    std::thread t2([&]() {
        fun2(chanVec);
    });
    std::this_thread::sleep_for(1s);
    printStatus(Channel::watchStatus(chanVec));
    t.join();
    t2.join();

    return 0;
}


void fun3(std::vector<Channel::Chan*>& chanVec) {
    std::this_thread::sleep_for(0s);
    shared_ptr<int> a = make_shared<int>(10), b = make_shared<int>(20);

    chanVec[0]->write(a,
    [](const std::string& selectName, const std::string& chanName, const std::any& a) {
        LOG("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    }
                );
    chanVec[0]->write(b, [](const std::string& selectName,
    const std::string& chanName, const std::any& a) {
        LOG("%s:write:%s:%d\n", selectName.c_str(), chanName.c_str(), *any_cast<shared_ptr<int>>(a));
        return true;
    });
}

void fun4(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a;
    chanVec[0]->read(a, [](const std::string& selectName,
                       const std::string& chanName, const std::any& a) {
        LOG("%s:read:%s:%d\n", selectName.c_str(), chanName.c_str(),
            *any_cast<shared_ptr<int>>(a));

        return true;
    });
}

int testBuffered() {
    Channel::Chan bchan1{1, "bchan1"}, bchan2{1, "bchan2"};
    std::vector<Channel::Chan*> chanVec{&bchan1, &bchan2};
    std::thread t([&]() {
        std::this_thread::sleep_for(1s);
        fun3(chanVec); //write
    });
    
    std::thread t2([&]() {
        
        fun4(chanVec); //read
    });

    t2.join();
    t.join();
    return 0;
}

void fun5(std::vector<Channel::Chan*>& chanVec) {
    shared_ptr<int> a = make_shared<int>(20);
    Channel::Select{
        "select1",
        Channel::Case{
            chanVec[0] >> a,
            taskRead
        },
        Channel::Default{
        }
    };
}

int testDefault() {
    Channel::Chan bchan1{0, "bchan1"};
    std::vector<Channel::Chan*> chanVec{&bchan1};
    std::thread t([&]() {
        std::this_thread::sleep_for(1s);
        fun5(chanVec); //write
    });

    t.join();
    return 0;
}

int main() {
    testNonBuffered();
    testBuffered();
    testDefault();
    return 0;
}