#pragma once

#include "value.h"
#include <map>
#include <string>
#include <memory>

namespace as3 {

class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr);

    // Variable operations
    void define(const std::string& name, ValuePtr value);
    ValuePtr get(const std::string& name);
    void set(const std::string& name, ValuePtr value);
    bool has(const std::string& name);

    // Scope management
    std::shared_ptr<Environment> createChild();
    std::shared_ptr<Environment> getParent() { return parent; }

private:
    std::map<std::string, ValuePtr> variables;
    std::shared_ptr<Environment> parent;
};

} // namespace as3
