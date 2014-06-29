#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <boost/any.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/version.hpp>
#include "queryplan.hpp"

#if !( (BOOST_VERSION / 100000) >= 1 && (BOOST_VERSION / 100 % 1000) >= 50)
#error "Boost-1.50 or newer is required: https://svn.boost.org/trac/boost/ticket/6785"
#endif

using namespace std;
using namespace boost;
using namespace boost::property_tree;

struct Start {
    void operator()(int& seed) {
        seed = rand();
    }
};

struct Extra {
    void operator()(int seed, int& result) {
        result = seed + rand();
    }
};

struct Add {
    void operator()(int a, int b, int& c) {
        c = a + b;
    }
};

struct Output {
    void operator()(int result) {
        cout << "\tresult=" << result << endl;
    }
};

struct Output2 {
    void operator()(long long result) {
        cout << "\tresult=" << result << endl;
    }
};

class DoSomething {
public:
    DoSomething(const ptree& config) : extra(0) {}

    void operator()(int a, int b, int& c, ostream& out) {
        c = a + b + extra;
        out << "a=" << a << " b=" << b << " c=" << c << "\n";
    }

private:
    int extra;
};

QP_MODULE(DoSomethingModule, "DoSomethingModule", DoSomething,
        ((QP_IN, int, a))
        ((QP_IN, int, b))
        ((QP_OUT, int&, c, 0))
        , (ostream&),
        const ptree&
);

QP_MODULE(StartModule, "StartModule", Start,
        ((QP_OUT, int&, seed, 0))
        , ()
);

QP_MODULE(ExtraModule, "ExtraModule", Extra,
        ((QP_IN, int, seed))
        ((QP_OUT, int&, result, 0))
        , ()
);

QP_MODULE(AddModule, "AddModule", Add,
        ((QP_IN, int, a))
        ((QP_IN, int, b))
        ((QP_OUT, int&, c, 0))
        , ()
);

QP_MODULE(OutputModule, "OutputModule", Output,
        ((QP_IN, int, result))
        , ()
);

QP_MODULE(Output2Module, "Output2Module", Output2,
        ((QP_IN, long long, result))
        , ()
);

void runModule(queryplan::Module<>& m)
{
    map<string, int> keys;
    keys["a"] = 0;
    keys["b"] = 1;
    keys["c"] = 2;

    m.resolve(keys);

    auto args = std::make_shared<queryplan::Context>();
    args->push_back(5);
    args->push_back(7);
    args->push_back(0);

    cout << any_cast<int>((*args)[0]) << ' ' << any_cast<int>((*args)[1]) << ' ' << any_cast<int>((*args)[2]) << "\n";

    if (0) {
        auto t0 = clock();
        const int count = 1000000;

        for (int i = 0; i < count; ++i) {
            m(args);
        }
        auto t1 = clock();

        cout << t0 << " " << t1 << " " << 1000000.0 * (t1 - t0) / CLOCKS_PER_SEC / count << "\n";
    } else {
        m(args);
    }

    cout << any_cast<int>((*args)[0]) << ' ' << any_cast<int>((*args)[1]) << ' ' << any_cast<int>((*args)[2]) << "\n";
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

    AddModule<> m("add");

    runModule(m);
    dumpModuleInfo(m.info());
}

void testRegisterModule()
{
    cout << __func__ << ":\n";

    auto factory = queryplan::getModuleFactoryRegistry<queryplan::Module<>>().find("AddModule");
    auto m = factory->create("add");

    runModule(*m);
    dumpModuleInfo(factory->info());

    delete m;
}

void loadQueryPlan(const char* filename)
{
    cout << "load query plan: " << filename << endl;

    ptree pt;
    read_json(filename, pt);

    queryplan::QueryPlan<queryplan::Module<>> qp(pt);

    cout << "numOutputs=" << qp.numOutputs() << endl;
    qp.writeGraphviz(cout);
}

void loadBadQueryPlan(const char* filename)
{
    try {
        loadQueryPlan(filename);
        assert("shouldn't reach here");
    } catch (std::exception& e) {
        cout << e.what() << endl;
    }
}

void testSingleThreadBlockedQueryPlanner(const char* filename)
{
    cout << __func__ << ": load query plan " << filename << endl;

    ptree pt;
    read_json(filename, pt);

    queryplan::SingleThreadBlockedQueryPlanner<queryplan::Module<>>
        planner(pt);

    planner();
}

void testSignalBasedSingleThreadBlockedQueryPlanner(const char* filename)
{
    cout << __func__ << ": load query plan " << filename << endl;

    ptree pt;
    read_json(filename, pt);

    queryplan::SignalBasedSingleThreadBlockedQueryPlanner<queryplan::Module<>>
        planner(pt);

    planner();
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    testDefineModule();

    cout << "\n";
    testRegisterModule();

    cout << "\n";
    loadQueryPlan("t/qp-example.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-unknown-module.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-conflict-output.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-wrong-inputs.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-wrong-outputs.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-miss-input.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-miss-output.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-unknown-output.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-self-depend.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-mismatch-type.json");

    cout << "\n";
    loadBadQueryPlan("t/qp-circular-dep.json");

    cout << "\n";
    testSingleThreadBlockedQueryPlanner("t/qp-example.json");

    cout << "\n";
    testSignalBasedSingleThreadBlockedQueryPlanner("t/qp-example.json");

    return 0;
}
