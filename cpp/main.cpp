#include <ctime>
#include <iostream>
#include <map>
#include <vector>
#include <boost/any.hpp>
#include "queryplan.hpp"

using namespace std;
using namespace boost;


void f(int a, int b, int& c) {
    c = a + b;
}

QP_MODULE(MyModule, "MyModule", decltype(&f),
        ((QP_IN, int, a))
        ((QP_IN, int, b))
        ((QP_OUT, int&, c, 0))
        , decltype(f));

void runModule(queryplan::Module& m)
{
    map<string, int> keys;
    keys["a"] = 0;
    keys["b"] = 1;
    keys["c"] = 2;

    m.resolve(keys);
    vector<any> args;
    args.push_back(5);
    args.push_back(7);
    args.push_back(0);

    cout << any_cast<int>(args[0]) << ' ' << any_cast<int>(args[1]) << ' ' << any_cast<int>(args[2]) << "\n";

    if (0) {
        auto t0 = clock();
        const int count = 1000000;

        for (int i = 0; i < count; ++i) {
            m.run(args);
        }
        auto t1 = clock();

        cout << t0 << " " << t1 << " " << 1000000.0 * (t1 - t0) / CLOCKS_PER_SEC / count << "\n";
    } else {
        m.run(args);
    }

    cout << any_cast<int>(args[0]) << ' ' << any_cast<int>(args[1]) << ' ' << any_cast<int>(args[2]) << "\n";
}

void dumpModuleInfo(const vector<queryplan::ArgInfo>& v)
{
    for (auto a : v) {
        cout << "(" << a.flag() << ", " << a.type() << ", " << a.name() << ", " << a.value() << ", " << a.typeinfo().name() << ")\n";
    }
}

void testDefineModule()
{
    cout << __func__ << ":\n";

    MyModule<decltype(f)> m("add", f);

    runModule(m);
    dumpModuleInfo(m.info());
}

void testRegisterModule()
{
    cout << __func__ << ":\n";

    auto factory = queryplan::getModuleFactoryRegistry<decltype(f)>().find("MyModule");
    auto m = factory->create("add", f);

    runModule(*m);
    dumpModuleInfo(factory->info());

    delete m;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    testDefineModule();
    cout << "\n";
    testRegisterModule();

    return 0;
}
