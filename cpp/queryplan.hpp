#ifndef QUERYPLAN__HPP__
#define QUERYPLAN__HPP__


#if __cplusplus < 201103L
#error requires C++11 features, try one of these compiler options: \
    -std=c++0x -std=c++11 -std=c++1y \
    -std=gnu++0x --std=gnu++11 -std=gnu++1y
#endif


// XXX: boost-1.55 sets it to 0 for clang compiler
#ifdef __clang__
    #if defined(BOOST_PP_VARIADICS)
        #if ! BOOST_PP_VARIADICS
        #error -DBOOST_PP_VARIADICS=1 is required on command line.
        #endif
    #else
        #define BOOST_PP_VARIADICS  1
    #endif
#endif


#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#include <boost/any.hpp>
#include <boost/preprocessor.hpp>
#include <boost/property_tree/ptree.hpp>


#define QP_IN       0
#define QP_OUT      1

#ifndef QP_ENABLE_TRACE
#define QP_ENABLE_TRACE     0
#endif

#ifndef QP_ENABLE_TIMING
#define QP_ENABLE_TIMING    0
#endif

#ifndef QP_TRACER
#define QP_TRACER           std::cerr
#endif


namespace queryplan {

class ArgInfo {
private:
    const int flag_;
    const char* type_;
    const char* name_;
    const char* value_;
    const std::type_info& typeinfo_;

public:
    ArgInfo(int flag, const char* type, const char* name,
            const char* value, const std::type_info& typeinfo) :
        flag_(flag), type_(type), name_(name),
        value_(value), typeinfo_(typeinfo) {}

    int flag() const { return flag_; }
    const char* type() const { return type_; }
    const char* name() const { return name_; }
    const char* value() const { return value_; }
    const std::type_info& typeinfo() const { return typeinfo_; }
};


class Module {
public:
    virtual void resolve(const std::map<std::string, int>& m) = 0;
    virtual void run(std::vector<boost::any>& v) = 0;
    virtual const std::string& id() const = 0;
    virtual ~Module() {}
};


template<typename... C>
class ModuleFactory {
public:
    virtual Module* create(const std::string& id, C... c) const = 0;
    virtual const std::vector<ArgInfo>& info() const = 0;
    virtual ~ModuleFactory() {}
};


template<typename ModuleT, typename... C>
class ConcreteModuleFactory : public ModuleFactory<C...> {
public:
    Module* create(const std::string& id, C... c) const {
        return new ModuleT(id, c...);
    }

    const std::vector<ArgInfo>& info() const {
        return ModuleT::info();
    }
};


template<typename... C>
class ModuleFactoryRegistry {
public:
    const ModuleFactory<C...>* find(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m);

        auto it = factories.find(name);
        if (it != factories.end()) {
            return it->second;
        } else {
            std::string msg = "module \"" + name + "\" not found";
            throw std::invalid_argument(msg);
        }
    }

    void insert(const std::string& name,
                       ModuleFactory<C...>* factory) {
        std::lock_guard<std::mutex> lock(m);

        if (! factories.insert(std::make_pair(name, factory)).second) {
            std::string msg = "module \"" + name + "\" is already registered";
            throw std::runtime_error(msg);
        }
    }

    std::map<std::string, ModuleFactory<C...>*> all() {
        std::lock_guard<std::mutex> lock(m);

        return std::map<std::string, ModuleFactory<C...>*>(factories);
    }

private:
    std::map<std::string, ModuleFactory<C...>*> factories;
    mutable std::mutex m;       // C++11 doesn't have shared_mutex
};


template<typename... C>
ModuleFactoryRegistry<C...>& getModuleFactoryRegistry() {
    static ModuleFactoryRegistry<C...> registry;

    return registry;
}


template<typename ModuleT, typename... C>
class ModuleFactoryRegister {
public:
    ModuleFactoryRegister(const std::string& name) {
        getModuleFactoryRegistry<C...>().insert(name, &factory);
    }

private:
    ConcreteModuleFactory<ModuleT, C...> factory;
};


template<typename... C>
class QueryPlan {
public:
    QueryPlan(const boost::property_tree::ptree& config, C... c) {
        std::vector<Module*> modules;
        std::map<std::string, OutputInfo> outputInfos;
        std::map<Module*, const std::vector<ArgInfo>*> argInfos;

        createModulesAndRecordOutputs(config, modules,
                outputInfos, argInfos,
                c...);

        connectInputsOutputs(config, modules,
                outputInfos, argInfos);

        checkCircularDependency();
    }

    ~QueryPlan() {
        for (auto d : dependencies) {
            delete d.first;
        }
    }

private:
    struct OutputInfo {
        Module* module;
        int index;
        const ArgInfo& arginfo;

        OutputInfo(Module* m, int i, const ArgInfo& a) :
            module(m), index(i), arginfo(a) {}
    };

    void createModulesAndRecordOutputs(
            const boost::property_tree::ptree& config,
            std::vector<Module*>& modules,
            std::map<std::string, OutputInfo>& outputInfos,
            std::map<Module*, const std::vector<ArgInfo>*>& argInfos,
            C... c) {
        for (auto& it : config) {
            const std::string& id = it.second.get<std::string>("id");
            auto factory = getModuleFactoryRegistry<C...>().find(
                    it.second.get<std::string>("module"));

            checkArguments(id, factory->info(), it.second);

            Module* m = factory->create(id, c...);
            modules.push_back(m);
            dependencies[m] = std::set<Module*>();
            argInfos[m] = &factory->info();

            auto outputs = it.second.find("outputs");
            if (outputs == it.second.not_found()) {
                continue;
            }

            for (auto& output : outputs->second) {
                const std::string& localName = output.first;
                const std::string& globalName =
                    output.second.get_value<std::string>();

                auto old = outputInfos.find(globalName);
                if (old == outputInfos.end()) {
                    outputInfos.insert(
                            std::make_pair(globalName,
                                OutputInfo(m, outputInfos.size(),
                                    findArgInfo(factory->info(),
                                        localName))));
                } else {
                    std::string msg = "module \"" +
                        old->second.module->id() +
                        "\" and module \"" + m->id() +
                        "\" output to same global name: " +
                        globalName;
                    throw std::invalid_argument(msg);
                }
            }
        }

        num_outputs = outputInfos.size();
    }

    void checkArguments(const std::string& id,
                        const std::vector<ArgInfo>& info,
                        const boost::property_tree::ptree& config) {
        auto inputs = config.find("inputs");
        auto outputs = config.find("outputs");
        size_t num_inputs = 0, num_outputs;

        for (auto& ai : info) {
            if (ai.flag() == QP_IN) {
                ++num_inputs;
            }
        }
        num_outputs = info.size() - num_inputs;

        if (num_inputs != (inputs == config.not_found() ?
                    0 : inputs->second.size())) {
            throw std::invalid_argument("module \"" + id +
                    "\" has inconsistent inputs between config and code");
        }

        if (num_outputs != (outputs == config.not_found() ?
                    0 : outputs->second.size())) {
            throw std::invalid_argument("module \"" + id +
                    "\" has inconsistent outputs between config and code");
        }

        for (auto& ai : info) {
            auto inouts = ai.flag() == QP_IN ? inputs : outputs;

            if (inouts->second.find(ai.name()) == inouts->second.not_found()) {
                auto msg = std::string("miss config for argument \"") +
                    ai.name() + "\" of module \"" + id + '"';
                throw std::invalid_argument(msg);
            }
        }
    }

    const ArgInfo& findArgInfo(const std::vector<ArgInfo>& info,
            const std::string& name) {
        for (auto& ai : info) {
            if (name == ai.name()) {
                return ai;
            }
        }

        // checkArguments() assures "name" must exists in "info".
        throw std::logic_error("shouldn't reach here");
    }

    void connectInputsOutputs(
            const boost::property_tree::ptree& config,
            const std::vector<Module*>& modules,
            const std::map<std::string, OutputInfo>& outputInfos,
            const std::map<Module*, const std::vector<ArgInfo>*>& argInfos) {
        int i = 0;

        for (auto& it : config) {
            const std::string& id = it.second.get<std::string>("id");
            Module* m = modules[i++];
            auto inputs = it.second.find("inputs");
            auto outputs = it.second.find("outputs");
            std::map<std::string, int> idx;

            if (outputs != it.second.not_found()) {
                for (auto& output : outputs->second) {
                    const std::string& localName = output.first;
                    const std::string& globalName =
                        output.second.get_value<std::string>();

                    recordLocalNames(outputInfos, idx,
                            localName, globalName);
                }
            }

            if (inputs != it.second.not_found()) {
                for (auto& input : inputs->second) {
                    const std::string& localName = input.first;
                    const std::string& globalName =
                        input.second.get_value<std::string>();

                    auto oi = outputInfos.find(globalName);
                    if (oi == outputInfos.end()) {
                        std::string msg = "input \"" + localName +
                            "\" of module \"" + id +
                            "\" has global name \"" + globalName +
                            "\" that doesn't bind to any known output";

                        throw std::invalid_argument(msg);
                    }

                    recordLocalNames(outputInfos, idx,
                            localName, globalName);

                    checkInputOutputType(id, localName,
                            findArgInfo(*argInfos.at(m), localName),
                            oi->second.arginfo);

                    Module* upstream = oi->second.module;
                    if (upstream == m) {
                        throw std::invalid_argument(
                                "self dependency found in module \"" +
                                m->id() + '"');
                    }
                    dependencies[upstream].insert(m);
                }
            }

            m->resolve(idx);
        }
    }

    void recordLocalNames(
            const std::map<std::string, OutputInfo>& outputInfos,
            std::map<std::string, int>& idx,
            const std::string& localName,
            const std::string& globalName) {
        if (idx.find(localName) != idx.end()) {
            // checkArguments() assures unique input/output names.
            throw std::logic_error("shouldn't reach here");
        }

        idx[localName] = outputInfos.at(globalName).index;
    }

    void checkInputOutputType(const std::string& id,
            const std::string& localName,
            const ArgInfo& i1, const ArgInfo& i2) {
        if (i1.typeinfo() != i2.typeinfo()) {
            std::string msg = "input \"" + localName +
                "\" of module \"" + id +
                "\" has different data type with its upstream output:\n" +
                "    internal type: " +
                i1.typeinfo().name() + "\n\targ name: " +
                i1.name() + "\n\targ type: " +
                i1.type() + '\n' +
                "    internal type: " +
                i2.typeinfo().name() + "\n\targ name: " +
                i2.name() + "\n\targ type: " +
                i2.type();
            throw std::invalid_argument(msg);
        }
    }

    void checkCircularDependency() {
        auto deps = dependencies;

        while (! deps.empty()) {
            bool found = false;

            for (auto it = deps.begin(); it != deps.end(); ++it) {
                if (it->second.empty()) {
                    Module* m = it->first;
                    found = true;

                    for (auto& d : deps) {
                        d.second.erase(m);
                    }

                    it = deps.erase(it);
                    if (it == deps.end()) {
                        break;
                    }
                }
            }

            if (! found) {
                std::string msg = "found circular dependency:\n";

                for (auto& it : deps) {
                    msg += '\t' + it.first->id() + " ->";

                    for (auto d : it.second) {
                        msg += ' ' + d->id();
                    }

                    msg += '\n';
                }

                throw std::invalid_argument(msg);
            }
        }
    }

    int num_outputs;
    std::map<Module*, std::set<Module*>> dependencies;
};


#define QP_MODULE(module, name, functorType, args, ...)     \
    QP_DEFINE_MODULE(module, functorType, args);            \
    QP_REGISTER_MODULE(module, name, ##__VA_ARGS__)



#define QP_DEFINE_MODULE(module, functorType, args) \
    template<typename... C>                         \
    class module : public queryplan::Module {       \
    public:                                         \
        module(const std::string& id,               \
             C... c) :                              \
            id_(id), func_(c...) {}                 \
        QP_DECLARE_RESOLVE(args)                    \
        QP_DECLARE_RUN(module, args)                \
        QP_DECLARE_MODULE_INFO(args)                \
        const std::string& id() const {             \
            return id_;                             \
        }                                           \
        functorType& functor()                      \
            const { return func_; }                 \
    private:                                        \
        const std::string id_;                      \
        functorType func_;                          \
        QP_DECLARE_INDEXES(args)                    \
    }



#define QP_REGISTER_MODULE(module, name, ...)       \
    static queryplan::ModuleFactoryRegister<        \
        module<__VA_ARGS__>, ##__VA_ARGS__>         \
            the##module##FactoryRegisterInstance(name)



#define QP_DECLARE_MODULE_INFO(args)        \
    static const                                            \
        std::vector<queryplan::ArgInfo>& info() {           \
            static const std::vector<queryplan::ArgInfo>    \
                v({ QP_MODULE_INFO(args) });                \
        return v;                                           \
    }

#define QP_MODULE_INFO(args)                \
    BOOST_PP_SEQ_ENUM(                      \
        BOOST_PP_SEQ_TRANSFORM(             \
            QP_ARG_INFO, 0, args))          \

#define QP_ARG_INFO(s, data, arg)           \
    queryplan::ArgInfo(                         \
        QP_ARG_FLAG(arg),                       \
        BOOST_PP_STRINGIZE(QP_ARG_TYPE(arg)),   \
        BOOST_PP_STRINGIZE(QP_ARG_NAME(arg)),   \
        BOOST_PP_STRINGIZE(QP_ARG_VALUE(arg)),  \
        typeid(QP_ARG_TYPE(arg)))



#define QP_DECLARE_RESOLVE(args)            \
    void resolve(const std::map<std::string, int>& m) {     \
        BOOST_PP_SEQ_FOR_EACH(QP_ASSIGN_INDEX, 0, args)     \
    }

#define QP_ASSIGN_INDEX(r, data, arg)       \
    QP_INDEX_NAME(arg) = m.at(BOOST_PP_STRINGIZE(QP_ARG_NAME(arg)));



#define QP_DECLARE_RUN(module, args)        \
    void run(std::vector<boost::any>& v) {                          \
        BOOST_PP_SEQ_FOR_EACH(QP_ASSIGN_VALUE, 0, args)             \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TRACE, QP_TRACE(module, args, ">"))           \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TIMING, QP_BEGIN_TIMING())                    \
        func_(BOOST_PP_SEQ_ENUM(                                    \
            BOOST_PP_SEQ_TRANSFORM(QP_TRANS_TYPE_NAME, 0, args)));  \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TIMING, QP_END_TIMING(module))                \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TRACE, QP_TRACE(module, args, "<"))           \
    }

#define QP_ASSIGN_VALUE(r, data, arg)       \
    boost::any& QP_ANY_NAME(arg) = v.at(QP_INDEX_NAME(arg));    \
    BOOST_PP_EXPR_IF(BOOST_PP_EQUAL(QP_ARG_FLAG(arg), QP_OUT),  \
            QP_ANY_NAME(arg) = QP_ARG_VALUE(arg);)

#define QP_TRANS_TYPE_NAME(s, data, arg)    \
    boost::any_cast<QP_ARG_TYPE(arg)>(QP_ANY_NAME(arg))

#define QP_ANY_NAME(arg)                    \
    BOOST_PP_SEQ_CAT((QP_ARG_NAME(arg)) (_any))

#define QP_TRACE(module, args, state)       \
    QP_TRACER << id_ << "(" #module ") " state  \
        BOOST_PP_SEQ_FOR_EACH(                  \
            QP_TRACE_ARG, 0, args)              \
        << "\n";

#define QP_TRACE_ARG(r, data, arg)         \
    << " " BOOST_PP_STRINGIZE(QP_ARG_NAME(arg)) "=" \
    << boost::any_cast<QP_ARG_TYPE(arg)>(QP_ANY_NAME(arg))

#define QP_BEGIN_TIMING()                   \
    auto wallclock_t0 = std::chrono::high_resolution_clock::now();      \
    auto realclock_t0 = std::clock();

#define QP_END_TIMING(module)               \
    auto realclock_t1 = std::clock();                                   \
    auto wallclock_t1 = std::chrono::high_resolution_clock::now();      \
    QP_TRACER << id_ << "(" #module ") spent "                          \
        << std::chrono::duration_cast<std::chrono::microseconds>(       \
            wallclock_t1 - wallclock_t0).count()                        \
        << " microseconds(wall) "                                       \
        << (long long)(1000000.0 * (realclock_t1 - realclock_t0)        \
            / CLOCKS_PER_SEC) << " microseconds(real)\n";



#define QP_DECLARE_INDEXES(args)            \
    BOOST_PP_SEQ_FOR_EACH(QP_DECLARE_INDEX, 0, args)

#define QP_DECLARE_INDEX(r, data, arg)      \
    int QP_INDEX_NAME(arg);

#define QP_INDEX_NAME(arg)                  \
    BOOST_PP_SEQ_CAT((QP_ARG_NAME(arg)) (_idx))



#define QP_ARG_FLAG(arg)    BOOST_PP_TUPLE_ELEM(0, arg)
#define QP_ARG_TYPE(arg)    BOOST_PP_TUPLE_ELEM(1, arg)
#define QP_ARG_NAME(arg)    BOOST_PP_TUPLE_ELEM(2, arg)
#define QP_ARG_VALUE(arg)   BOOST_PP_TUPLE_ELEM(3, arg)

}   /* namespace queryplan */

#endif  /* QUERYPLAN__HPP__ */

