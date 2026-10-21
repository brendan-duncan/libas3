// Namespace and QName (E4X): a namespace's prefix and URI, and a name in
// one. Made as the E4X standard has them made from what they are given,
// with the errors the player gives.

#include "builtins.h"

#include <cctype>
#include <optional>

namespace as3 {

namespace {

// Each has its parts as properties of its own, which for-in visits.
struct NamespaceHost : HostObject {
    // None: undefined (a prefix not known).
    std::optional<std::string> prefix;
    std::string uri;

    ValuePtr get(const std::string& name) override {
        if (name == "uri") return Value::makeString(uri);
        if (name == "prefix") return prefix ? Value::makeString(*prefix) : Value::makeUndefined();
        return nullptr;
    }
    bool enumerate(std::vector<Val>& keys) override {
        keys.push_back(Val::string("uri"));
        keys.push_back(Val::string("prefix"));
        return true;
    }
};

struct QNameHost : HostObject {
    std::string localName;
    // None: null, any namespace.
    std::optional<std::string> uri;

    ValuePtr get(const std::string& name) override {
        if (name == "uri") return uri ? Value::makeString(*uri) : Value::makeNull();
        if (name == "localName") return Value::makeString(localName);
        return nullptr;
    }
    bool enumerate(std::vector<Val>& keys) override {
        keys.push_back(Val::string("uri"));
        keys.push_back(Val::string("localName"));
        return true;
    }
};

// A name XML allows: a letter or _ first, then letters, digits, ., - and _.
bool isXmlName(const std::string& name) {
    if (name.empty()) return false;
    const auto letter = [](unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; };
    if (!letter(static_cast<unsigned char>(name[0]))) return false;
    for (unsigned char c : name) {
        if (!letter(c) && !std::isdigit(c) && c != '.' && c != '-') return false;
    }
    return true;
}

// The URI a value names: a namespace's or a qualified name's, or the value
// as text.
std::string uriOf(VM& vm, const Val& value) {
    if (const NamespaceHost* ns = value.host<NamespaceHost>()) return ns->uri;
    if (const QNameHost* q = value.host<QNameHost>()) {
        if (q->uri) return *q->uri;
    }
    return vm.toString(value);
}

std::string qnameText(const QNameHost& q) {
    if (!q.uri) return "*::" + q.localName;
    if (q.uri->empty()) return q.localName;
    return *q.uri + "::" + q.localName;
}

void makeNamespace(VM& vm, const Val& self, Args args) {
    auto made = std::make_shared<NamespaceHost>();
    if (args.size() == 0) {
        made->prefix = std::string();
    } else if (args.size() == 1) {
        if (const NamespaceHost* from = args[0].host<NamespaceHost>()) {
            *made = *from;
        } else {
            made->uri = uriOf(vm, args[0]);
            if (made->uri.empty()) made->prefix = std::string();
        }
    } else {
        made->uri = uriOf(vm, args[1]);
        const Val& prefix = args[0];
        if (made->uri.empty()) {
            // Only the empty prefix goes with no namespace.
            if (prefix.isNullish() || !vm.toString(prefix).empty()) {
                vm.throwError("TypeError",
                              "Error #1098: Illegal prefix " + vm.toString(prefix) + " for no namespace.", 1098);
            }
            made->prefix = std::string();
        } else if (!prefix.isNullish()) {
            const std::string text = vm.toString(prefix);
            if (text.empty() || isXmlName(text)) made->prefix = text;
        }
    }
    self->host = made;
}

void makeQName(VM& vm, const Val& self, Args args) {
    auto made = std::make_shared<QNameHost>();
    Val name = args.size() >= 2 ? args[1] : args[0];
    const bool hasNamespace = args.size() >= 2;
    if (const QNameHost* from = name.host<QNameHost>()) {
        if (!hasNamespace) {
            *made = *from;
            self->host = made;
            return;
        }
        made->localName = from->localName;
    } else {
        made->localName = name.isUndefined() ? std::string() : vm.toString(name);
    }
    if (!hasNamespace || args[0].isUndefined()) {
        // The default namespace, or any for *.
        if (made->localName == "*") {
            made->uri.reset();
        } else {
            made->uri = std::string();
        }
    } else if (args[0].isNull()) {
        made->uri.reset();
    } else {
        made->uri = uriOf(vm, args[0]);
    }
    self->host = made;
}

} // namespace

void defineNamespaceBuiltins(VM& vm) {
    vm.defineNativeClass("Namespace")
        .final()
        .constructor([](VM& vm, const Val& self, Args args) {
            makeNamespace(vm, self, args);
            return Val();
        })
        // Called as a function, a namespace is itself.
        .callHandler([](VM& vm, const Val&, Args args) {
            if (args.size() == 1 && args[0].host<NamespaceHost>()) return args[0];
            std::vector<Val> list(args.begin(), args.end());
            return vm.construct(Val(vm.getGlobal("Namespace")), list);
        })
        .getter("prefix", [](VM&, const Val& self, Args) {
            const NamespaceHost* ns = self.host<NamespaceHost>();
            return ns && ns->prefix ? Val::string(*ns->prefix) : Val();
        })
        .getter("uri", [](VM&, const Val& self, Args) {
            const NamespaceHost* ns = self.host<NamespaceHost>();
            return Val::string(ns ? ns->uri : std::string());
        })
        .method("toString", [](VM&, const Val& self, Args) {
            const NamespaceHost* ns = self.host<NamespaceHost>();
            return Val::string(ns ? ns->uri : std::string());
        })
        .method("valueOf", [](VM&, const Val& self, Args) {
            const NamespaceHost* ns = self.host<NamespaceHost>();
            return Val::string(ns ? ns->uri : std::string());
        })
        .define();

    vm.defineNativeClass("QName")
        .final()
        .constructor([](VM& vm, const Val& self, Args args) {
            makeQName(vm, self, args);
            return Val();
        })
        .callHandler([](VM& vm, const Val&, Args args) {
            if (args.size() == 1 && args[0].host<QNameHost>()) return args[0];
            std::vector<Val> list(args.begin(), args.end());
            return vm.construct(Val(vm.getGlobal("QName")), list);
        })
        .getter("localName", [](VM&, const Val& self, Args) {
            const QNameHost* q = self.host<QNameHost>();
            return Val::string(q ? q->localName : std::string());
        })
        .getter("uri", [](VM&, const Val& self, Args) {
            const QNameHost* q = self.host<QNameHost>();
            return q && q->uri ? Val::string(*q->uri) : Val::null();
        })
        .method("toString", [](VM&, const Val& self, Args) {
            const QNameHost* q = self.host<QNameHost>();
            return Val::string(q ? qnameText(*q) : std::string());
        })
        .method("valueOf", [](VM&, const Val& self, Args) { return self; })
        .define();
}

Val makeNamespaceValue(VM& vm, const std::string& uri) {
    const Val cls(vm.getGlobal("Namespace"));
    if (!cls.isObject()) return Val::string(uri);
    return vm.construct(cls, std::vector<Val>{Val::string(uri)});
}

} // namespace as3
