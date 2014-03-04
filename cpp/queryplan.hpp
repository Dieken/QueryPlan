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
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>
#include <boost/any.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/topological_sort.hpp>
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


template<typename... A>
class Module {
public:
    virtual void resolve(const std::map<std::string, int>& m) = 0;
    virtual void operator()(std::vector<boost::any>& v, A... a) = 0;
    virtual const std::string& id() const = 0;
    virtual ~Module() {}
};


template<typename M, typename... C>
class ModuleFactory {
public:
    virtual M* create(const std::string& id, C... c) const = 0;
    virtual const std::vector<ArgInfo>& info() const = 0;
    virtual ~ModuleFactory() {}
};


template<typename ModuleT, typename M, typename... C>
class ConcreteModuleFactory : public ModuleFactory<M, C...> {
public:
    M* create(const std::string& id, C... c) const {
        return new ModuleT(id, c...);
    }

    const std::vector<ArgInfo>& info() const {
        return ModuleT::info();
    }
};


template<typename M, typename... C>
class ModuleFactoryRegistry {
public:
    const ModuleFactory<M, C...>* find(const std::string& name) const {
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
                       ModuleFactory<M, C...>* factory) {
        std::lock_guard<std::mutex> lock(m);

        if (! factories.insert(std::make_pair(name, factory)).second) {
            std::string msg = "module \"" + name + "\" is already registered";
            throw std::runtime_error(msg);
        }
    }

    std::map<std::string, ModuleFactory<M, C...>*> all() {
        std::lock_guard<std::mutex> lock(m);

        return std::map<std::string, ModuleFactory<M, C...>*>(factories);
    }

private:
    std::map<std::string, ModuleFactory<M, C...>*> factories;
    mutable std::mutex m;       // C++11 doesn't have shared_mutex
};


template<typename M, typename... C>
ModuleFactoryRegistry<M, C...>& getModuleFactoryRegistry() {
    static ModuleFactoryRegistry<M, C...> registry;

    return registry;
}


template<typename ModuleT, typename M, typename... C>
class ModuleFactoryRegister {
public:
    ModuleFactoryRegister(const std::string& name) {
        getModuleFactoryRegistry<M, C...>().insert(name, &factory);
    }

private:
    ConcreteModuleFactory<ModuleT, M, C...> factory;
};


template<typename M, typename... C>
class QueryPlan {
public:
    typedef boost::adjacency_list<boost::vecS, boost::vecS,
            boost::bidirectionalS, std::shared_ptr<M>> Graph;

    QueryPlan(const boost::property_tree::ptree& config, C... c) {
        G dependencies;
        std::map<std::string, OutputInfo> outputInfos;
        std::map<Vertex, const std::vector<ArgInfo>*> argInfos;

        createModulesAndRecordOutputs(config, dependencies,
                outputInfos, argInfos, c...);

        connectInputsOutputs(config, dependencies,
                outputInfos, argInfos);

        checkCircularDependency(dependencies);

        boost::copy_graph(dependencies, graph);
    }

    int numOutputs() {
        return num_outputs;
    }

    Graph& dependencies() {
        return graph;
    }

    void writeGraphviz(std::ostream& out) {
        writeGraphviz(out, graph);
    }

private:
    typedef boost::adjacency_list<boost::setS, boost::vecS,
            boost::directedS, std::shared_ptr<M>> G;
    typedef typename G::vertex_descriptor Vertex;

    struct OutputInfo {
        Vertex module;
        int index;
        const ArgInfo& arginfo;

        OutputInfo(Vertex m, int i, const ArgInfo& a) :
            module(m), index(i), arginfo(a) {}
    };

    struct VertexPropertyWriter {
        const Graph& graph;

        VertexPropertyWriter(const Graph& g) : graph(g) {}

        void operator()(std::ostream& out,
                        const typename Graph::vertex_descriptor& v) {
            out << "[label=\"" << graph[v]->id() << "\"]";
        }
    };

    template<typename T>
    static void writeGraphviz(std::ostream& out, const T& graph) {
#ifdef QP_ENABLE_BOOST_WRITE_GRAPHVIZ
        // its output isn't easy to grep due to numbered vertex ID.
        boost::write_graphviz(out, graph, VertexPropertyWriter(graph));
#else

        out << "digraph G {\n";

        typename boost::graph_traits<T>::vertex_iterator v, v_end;
        for (std::tie(v, v_end) = boost::vertices(graph); v != v_end; ++v) {
            out << "  \"" << graph[*v]->id() << "\";\n";

            std::string parent = "\t\t\"" + graph[*v]->id() + "\" -> \"";
            typename boost::graph_traits<T>::adjacency_iterator a, a_end;

            for (std::tie(a, a_end) = boost::adjacent_vertices(*v, graph); a != a_end; ++a) {
                out << parent << graph[*a]->id() << "\";\n";
            }
        }

        out << "}\n";
#endif
    }

    void createModulesAndRecordOutputs(
            const boost::property_tree::ptree& config,
            G& dependencies,
            std::map<std::string, OutputInfo>& outputInfos,
            std::map<Vertex, const std::vector<ArgInfo>*>& argInfos,
            C... c) {
        for (auto& it : config) {
            const std::string& id = it.second.get<std::string>("id");
            auto factory = getModuleFactoryRegistry<M, C...>().find(
                    it.second.get<std::string>("module"));

            checkArguments(id, factory->info(), it.second);

            Vertex m = boost::add_vertex(
                    std::shared_ptr<M>(factory->create(id, c...)),
                    dependencies);
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
                        dependencies[old->second.module]->id() +
                        "\" and module \"" + dependencies[m]->id() +
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
            G& dependencies,
            const std::map<std::string, OutputInfo>& outputInfos,
            const std::map<Vertex, const std::vector<ArgInfo>*>& argInfos) {
        typename boost::graph_traits<G>::vertex_iterator v, v_end;
        std::tie(v, v_end) = boost::vertices(dependencies);

        for (auto& it : config) {
            const std::string& id = it.second.get<std::string>("id");

            auto m = *v;
            ++v;

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

                    auto upstream = oi->second.module;
                    if (upstream == m) {
                        throw std::invalid_argument(
                                "self dependency found in module \"" +
                                dependencies[m]->id() + '"');
                    }
                    boost::add_edge(upstream, m, dependencies);
                }
            }

            dependencies[m]->resolve(idx);
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

    void checkCircularDependency(G deps) {
        while (boost::num_vertices(deps) > 0) {
            bool found = false;

            typename boost::graph_traits<G>::vertex_iterator v, v_end;
            for (std::tie(v, v_end) = vertices(deps); v != v_end; ++v) {
                if (out_degree(*v, deps) == 0) {
                    found = true;
                    clear_vertex(*v, deps);
                    remove_vertex(*v, deps);
                    break;
                }
            }

            if (! found) {
                std::ostringstream out;
                out << "found circular dependency:\n";
                writeGraphviz(out, deps);
                throw std::invalid_argument(out.str());
            }
        }
    }

    int num_outputs;
    Graph graph;
};


template<typename M, typename... C>
class SingleThreadBlockedQueryPlanner
{
public:
    SingleThreadBlockedQueryPlanner(
            const boost::property_tree::ptree& config, C... c) {
        QueryPlan<M, C...> plan(config, c...);

        num_outputs = plan.numOutputs();
        G& g = plan.dependencies();

        std::vector<Vertex> v;
        boost::topological_sort(g, std::back_inserter(v));

        modules.reserve(v.size());

        for (auto it = v.rbegin(); it != v.rend(); ++it) {
            modules.push_back(g[*it]);
        }
    }

    template<typename... A>
    void operator()(A... a) {
        std::vector<boost::any> v(num_outputs);

        for (auto& m : modules) {
            (*m)(v, a...);
        }
    }

private:
    typedef typename QueryPlan<M, C...>::Graph G;
    typedef typename G::vertex_descriptor Vertex;

    int num_outputs;
    std::vector<std::shared_ptr<M>> modules;
};



#define QP_MODULE(module, name, functorType, args,          \
                  extra_args, ...)                          \
    QP_DEFINE_MODULE(module, functorType, args);            \
    QP_REGISTER_MODULE(module, name, extra_args, ##__VA_ARGS__)



#define QP_DEFINE_MODULE(module, functorType, args)         \
    template<typename... A>                                 \
    class module : public queryplan::Module<A...> {         \
    public:                                                 \
        template<typename... C>                             \
        module(const std::string& id,                       \
             C... c) :                                      \
            id_(id), func_(c...) {}                         \
        QP_DECLARE_RESOLVE(args)                            \
        QP_DECLARE_RUN(module, args)                        \
        QP_DECLARE_MODULE_INFO(args)                        \
        const std::string& id() const {                     \
            return id_;                                     \
        }                                                   \
        functorType& functor()                              \
            const { return func_; }                         \
    private:                                                \
        const std::string id_;                              \
        functorType func_;                                  \
        QP_DECLARE_INDEXES(args)                            \
    }



#define QP_REGISTER_MODULE(module, name, extra_args, ...)   \
    static queryplan::ModuleFactoryRegister<                \
        module<QP_STRIP_PAREN extra_args>,                  \
        queryplan::Module<QP_STRIP_PAREN extra_args>,       \
        ##__VA_ARGS__>                                      \
            the##module##FactoryRegisterInstance(name)

#define QP_STRIP_PAREN(...) __VA_ARGS__



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
    void operator()(std::vector<boost::any>& v, A... a) {           \
        BOOST_PP_SEQ_FOR_EACH(QP_ASSIGN_VALUE, 0, args)             \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TRACE, QP_TRACE(module, args, ">"))           \
        BOOST_PP_EXPR_IF(                                           \
            QP_ENABLE_TIMING, QP_BEGIN_TIMING())                    \
        func_(BOOST_PP_SEQ_ENUM(                                    \
            BOOST_PP_SEQ_TRANSFORM(QP_TRANS_TYPE_NAME, 0, args)),   \
              a...);                                                \
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

