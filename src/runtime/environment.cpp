#include "environment.h"
#include <stdexcept>

namespace as3 {

Environment::Environment(std::shared_ptr<Environment> parent)
    : parent(parent) {
}

void Environment::define(const std::string& name, ValuePtr value) {
    variables[name] = value;
}

ValuePtr Environment::get(const std::string& name) {
    auto it = variables.find(name);
    if (it != variables.end()) {
        return it->second;
    }

    if (parent) {
        return parent->get(name);
    }

    // Return undefined for missing variables (AS3 behavior)
    return Value::makeUndefined();
}

void Environment::set(const std::string& name, ValuePtr value) {
    auto it = variables.find(name);
    if (it != variables.end()) {
        it->second = value;
        return;
    }

    if (parent) {
        parent->set(name, value);
        return;
    }

    // If variable doesn't exist, create it in current scope (AS3 dynamic behavior)
    variables[name] = value;
}

bool Environment::has(const std::string& name) {
    if (variables.find(name) != variables.end()) {
        return true;
    }

    if (parent) {
        return parent->has(name);
    }

    return false;
}

std::shared_ptr<Environment> Environment::createChild() {
    return std::make_shared<Environment>(shared_from_this());
}

} // namespace as3
