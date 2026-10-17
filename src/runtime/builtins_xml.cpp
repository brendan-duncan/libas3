// XML and XMLList (E4X): XML parsed into a tree of nodes; x.name gives the
// children of that name, x.@name an attribute, x..name the descendants,
// x.(test) those items the test is true of. An XML is a list of one, as
// E4X has it.

#include "builtins.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace as3 {

namespace {

struct XmlNode;
using NodePtr = std::shared_ptr<XmlNode>;

struct XmlNode {
    enum class Kind { Element, Text, Comment, Instruction, Attribute };
    Kind kind = Kind::Element;
    // An element's, attribute's or instruction's name.
    std::string name;
    // Text, a comment's or instruction's content, an attribute's value.
    std::string value;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<NodePtr> children;
    // The element it is in (an attribute's, its element).
    std::weak_ptr<XmlNode> parent;
};

// XML's settings (the static ones on XML).
struct XmlSettings {
    bool ignoreComments = true;
    bool ignoreProcessingInstructions = true;
    bool ignoreWhitespace = true;
    bool prettyPrinting = true;
    int prettyIndent = 2;
};

XmlSettings& settings() {
    static XmlSettings current;
    return current;
}

Val settingsObject(VM& vm, const XmlSettings& set) {
    Val object = vm.newObject();
    vm.setProperty(object, "ignoreComments", Val::boolean(set.ignoreComments));
    vm.setProperty(object, "ignoreProcessingInstructions", Val::boolean(set.ignoreProcessingInstructions));
    vm.setProperty(object, "ignoreWhitespace", Val::boolean(set.ignoreWhitespace));
    vm.setProperty(object, "prettyPrinting", Val::boolean(set.prettyPrinting));
    vm.setProperty(object, "prettyIndent", Val::number(set.prettyIndent));
    return object;
}

// Parsing

class XmlReader {
public:
    XmlReader(VM& vm, const std::string& text) : _vm(vm), _text(text) {}

    std::vector<NodePtr> readAll() {
        std::vector<NodePtr> nodes;
        while (_at < _text.size()) {
            if (_text.compare(_at, 5, "<?xml") == 0) {
                // The declaration.
                const size_t end = _text.find("?>", _at);
                _at = end == std::string::npos ? _text.size() : end + 2;
                continue;
            }
            if (_text.compare(_at, 9, "<!DOCTYPE") == 0) {
                const size_t end = _text.find('>', _at);
                _at = end == std::string::npos ? _text.size() : end + 1;
                continue;
            }
            readNode(nodes, nullptr);
        }
        return nodes;
    }

private:
    VM& _vm;
    const std::string& _text;
    size_t _at = 0;

    [[noreturn]] void fail(const char* what) {
        _vm.throwError("TypeError", std::string("Error #1090: XML parser failure: ") + what + ".", 1090);
    }

    static bool isNameChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.' ||
               static_cast<unsigned char>(c) >= 0x80;
    }

    std::string readName() {
        const size_t start = _at;
        while (_at < _text.size() && isNameChar(_text[_at])) ++_at;
        if (_at == start) fail("element is malformed");
        return _text.substr(start, _at - start);
    }

    void skipSpace() {
        while (_at < _text.size() && std::isspace(static_cast<unsigned char>(_text[_at]))) ++_at;
    }

    std::string decode(const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '&') {
                out += text[i];
                continue;
            }
            const size_t semi = text.find(';', i);
            if (semi == std::string::npos) {
                out += text[i];
                continue;
            }
            const std::string entity = text.substr(i + 1, semi - i - 1);
            uint32_t code = 0;
            if (entity == "lt") code = '<';
            else if (entity == "gt") code = '>';
            else if (entity == "amp") code = '&';
            else if (entity == "quot") code = '"';
            else if (entity == "apos") code = '\'';
            else if (!entity.empty() && entity[0] == '#') {
                code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                           ? static_cast<uint32_t>(std::strtoul(entity.c_str() + 2, nullptr, 16))
                           : static_cast<uint32_t>(std::strtoul(entity.c_str() + 1, nullptr, 10));
            }
            if (code == 0) {
                out += text[i];
                continue;
            }
            std::u16string unit;
            if (code >= 0x10000) {
                code -= 0x10000;
                unit += static_cast<char16_t>(0xD800 + (code >> 10));
                unit += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
            } else {
                unit += static_cast<char16_t>(code);
            }
            out += fromUtf16(unit);
            i = semi;
        }
        return out;
    }

    void addText(std::vector<NodePtr>& into, const NodePtr& parent, const std::string& raw, bool cdata) {
        std::string text = cdata ? raw : decode(raw);
        if (!cdata && settings().ignoreWhitespace) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return;
            const size_t last = text.find_last_not_of(" \t\r\n");
            text = text.substr(first, last - first + 1);
        }
        auto node = std::make_shared<XmlNode>();
        node->kind = XmlNode::Kind::Text;
        node->value = text;
        node->parent = parent;
        into.push_back(node);
    }

    void readNode(std::vector<NodePtr>& into, const NodePtr& parent) {
        if (_text[_at] != '<') {
            const size_t next = _text.find('<', _at);
            const size_t end = next == std::string::npos ? _text.size() : next;
            addText(into, parent, _text.substr(_at, end - _at), false);
            _at = end;
            return;
        }
        if (_text.compare(_at, 4, "<!--") == 0) {
            const size_t end = _text.find("-->", _at);
            if (end == std::string::npos) fail("unterminated comment");
            if (!settings().ignoreComments) {
                auto node = std::make_shared<XmlNode>();
                node->kind = XmlNode::Kind::Comment;
                node->value = _text.substr(_at + 4, end - _at - 4);
                node->parent = parent;
                into.push_back(node);
            }
            _at = end + 3;
            return;
        }
        if (_text.compare(_at, 9, "<![CDATA[") == 0) {
            const size_t end = _text.find("]]>", _at);
            if (end == std::string::npos) fail("unterminated CDATA section");
            addText(into, parent, _text.substr(_at + 9, end - _at - 9), true);
            _at = end + 3;
            return;
        }
        if (_text.compare(_at, 2, "<?") == 0) {
            const size_t end = _text.find("?>", _at);
            if (end == std::string::npos) fail("unterminated processing instruction");
            if (!settings().ignoreProcessingInstructions) {
                auto node = std::make_shared<XmlNode>();
                node->kind = XmlNode::Kind::Instruction;
                const std::string body = _text.substr(_at + 2, end - _at - 2);
                const size_t space = body.find_first_of(" \t\r\n");
                node->name = body.substr(0, space);
                node->value = space == std::string::npos ? std::string() : body.substr(space + 1);
                node->parent = parent;
                into.push_back(node);
            }
            _at = end + 2;
            return;
        }
        if (_text.compare(_at, 2, "</") == 0) {
            fail("unexpected end tag");
        }
        ++_at;  // <
        auto element = std::make_shared<XmlNode>();
        element->kind = XmlNode::Kind::Element;
        element->parent = parent;
        element->name = readName();
        for (;;) {
            skipSpace();
            if (_at >= _text.size()) fail("element is malformed");
            if (_text[_at] == '/') {
                if (_text.compare(_at, 2, "/>") != 0) fail("element is malformed");
                _at += 2;
                into.push_back(element);
                return;
            }
            if (_text[_at] == '>') {
                ++_at;
                break;
            }
            const std::string name = readName();
            skipSpace();
            if (_at >= _text.size() || _text[_at] != '=') fail("attribute is malformed");
            ++_at;
            skipSpace();
            if (_at >= _text.size() || (_text[_at] != '"' && _text[_at] != '\'')) fail("attribute is malformed");
            const char quote = _text[_at++];
            const size_t end = _text.find(quote, _at);
            if (end == std::string::npos) fail("attribute is malformed");
            element->attributes.emplace_back(name, decode(_text.substr(_at, end - _at)));
            _at = end + 1;
        }
        // The children, to </name>.
        for (;;) {
            if (_at >= _text.size()) fail("element is not closed");
            if (_text.compare(_at, 2, "</") == 0) {
                _at += 2;
                const std::string name = readName();
                skipSpace();
                if (name != element->name || _at >= _text.size() || _text[_at] != '>') {
                    _vm.throwError("TypeError",
                                   "Error #1085: The element type \"" + element->name +
                                       "\" must be terminated by the matching end-tag \"</" + element->name + ">\".",
                                   1085);
                }
                ++_at;
                break;
            }
            readNode(element->children, element);
        }
        into.push_back(element);
    }
};

// Writing

std::string escapeText(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string escapeAttribute(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool hasSimpleContent(const XmlNode& node) {
    if (node.kind != XmlNode::Kind::Element) {
        return node.kind == XmlNode::Kind::Text || node.kind == XmlNode::Kind::Attribute;
    }
    for (const auto& child : node.children) {
        if (child->kind == XmlNode::Kind::Element) return false;
    }
    return true;
}

void write(const XmlNode& node, int level, std::string& out) {
    const XmlSettings& set = settings();
    const std::string indent = set.prettyPrinting ? std::string(static_cast<size_t>(level * set.prettyIndent), ' ')
                                                  : std::string();
    switch (node.kind) {
        case XmlNode::Kind::Text:
            out += indent + escapeText(node.value);
            return;
        case XmlNode::Kind::Attribute:
            out += escapeAttribute(node.value);
            return;
        case XmlNode::Kind::Comment:
            out += indent + "<!--" + node.value + "-->";
            return;
        case XmlNode::Kind::Instruction:
            out += indent + "<?" + node.name + (node.value.empty() ? "" : " " + node.value) + "?>";
            return;
        case XmlNode::Kind::Element:
            break;
    }
    out += indent + "<" + node.name;
    for (const auto& attribute : node.attributes) {
        out += " " + attribute.first + "=\"" + escapeAttribute(attribute.second) + "\"";
    }
    if (node.children.empty()) {
        out += "/>";
        return;
    }
    out += ">";
    // Text alone stays on the line.
    bool onlyText = true;
    for (const auto& child : node.children) {
        if (child->kind != XmlNode::Kind::Text) onlyText = false;
    }
    if (onlyText || !set.prettyPrinting) {
        for (const auto& child : node.children) {
            if (child->kind == XmlNode::Kind::Text) {
                out += escapeText(child->value);
            } else {
                write(*child, 0, out);
            }
        }
    } else {
        for (const auto& child : node.children) {
            out += "\n";
            write(*child, level + 1, out);
        }
        out += "\n" + indent;
    }
    out += "</" + node.name + ">";
}

std::string toXmlString(const XmlNode& node) {
    std::string out;
    write(node, 0, out);
    return out;
}

// What String(xml) gives: an element of simple content its text, else its
// markup.
std::string textOf(const XmlNode& node) {
    if (node.kind == XmlNode::Kind::Text || node.kind == XmlNode::Kind::Attribute) return node.value;
    if (node.kind == XmlNode::Kind::Element && hasSimpleContent(node)) {
        std::string text;
        for (const auto& child : node.children) {
            if (child->kind == XmlNode::Kind::Text) text += child->value;
        }
        return text;
    }
    return toXmlString(node);
}

NodePtr copyNode(const XmlNode& node, const NodePtr& parent) {
    auto made = std::make_shared<XmlNode>();
    made->kind = node.kind;
    made->name = node.name;
    made->value = node.value;
    made->attributes = node.attributes;
    made->parent = parent;
    for (const auto& child : node.children) {
        made->children.push_back(copyNode(*child, made));
    }
    return made;
}

NodePtr textNode(const std::string& text, const NodePtr& parent) {
    auto node = std::make_shared<XmlNode>();
    node->kind = XmlNode::Kind::Text;
    node->value = text;
    node->parent = parent;
    return node;
}

NodePtr attributeNode(const NodePtr& element, const std::pair<std::string, std::string>& attribute) {
    auto node = std::make_shared<XmlNode>();
    node->kind = XmlNode::Kind::Attribute;
    node->name = attribute.first;
    node->value = attribute.second;
    node->parent = element;
    return node;
}

// The objects scripts hold.

struct XmlHost;
struct XmlListHost;

Val makeList(VM& vm, std::vector<NodePtr> nodes, NodePtr target = nullptr, const std::string& targetName = std::string());
Val makeXml(VM& vm, NodePtr node);
std::vector<NodePtr> nodesOf(VM& vm, const Val& value);

// The nodes a name picks from a node: children of that name (or all, *),
// or with @ an attribute (or all, @*).
std::vector<NodePtr> pick(const NodePtr& node, const std::string& name) {
    std::vector<NodePtr> found;
    if (node->kind != XmlNode::Kind::Element) {
        return found;
    }
    if (!name.empty() && name[0] == '@') {
        const std::string attribute = name.substr(1);
        for (const auto& pair : node->attributes) {
            if (attribute == "*" || pair.first == attribute) found.push_back(attributeNode(node, pair));
        }
        return found;
    }
    for (const auto& child : node->children) {
        if (name == "*" || (child->kind == XmlNode::Kind::Element && child->name == name)) found.push_back(child);
    }
    return found;
}

void descendants(const NodePtr& node, const std::string& name, std::vector<NodePtr>& found) {
    if (node->kind != XmlNode::Kind::Element) return;
    if (!name.empty() && name[0] == '@') {
        for (const auto& attribute : pick(node, name)) found.push_back(attribute);
    }
    for (const auto& child : node->children) {
        if (name.empty() || name[0] != '@') {
            if (name == "*" || (child->kind == XmlNode::Kind::Element && child->name == name)) found.push_back(child);
        }
        descendants(child, name, found);
    }
}

// Sets x.name = value: an attribute, or the children of that name (made
// when there are none), as E4X's [[Put]] does.
void putChild(VM& vm, const NodePtr& node, const std::string& name, const Val& value) {
    if (node->kind != XmlNode::Kind::Element) return;
    if (!name.empty() && name[0] == '@') {
        const std::string attribute = name.substr(1);
        const std::string text = value.isObject() && (value.host<XmlHost>() || value.host<XmlListHost>())
                                     ? vm.toString(value)
                                     : value.toString();
        for (auto& pair : node->attributes) {
            if (pair.first == attribute) {
                pair.second = text;
                return;
            }
        }
        node->attributes.emplace_back(attribute, text);
        return;
    }
    // What to put: copies of XML, or text.
    std::vector<NodePtr> content;
    const bool isXml = value.isObject() && (value.host<XmlHost>() || value.host<XmlListHost>());
    if (isXml) {
        for (const auto& item : nodesOf(vm, value)) content.push_back(copyNode(*item, node));
    }
    if (name == "*") {
        node->children.clear();
        if (isXml) {
            node->children = content;
        } else {
            node->children.push_back(textNode(vm.toString(value), node));
        }
        return;
    }
    auto& children = node->children;
    auto first = std::find_if(children.begin(), children.end(), [&name](const NodePtr& child) {
        return child->kind == XmlNode::Kind::Element && child->name == name;
    });
    if (first == children.end()) {
        auto element = std::make_shared<XmlNode>();
        element->name = name;
        element->parent = node;
        if (isXml) {
            for (auto& item : content) item->parent = element;
            element->children = content;
        } else {
            element->children.push_back(textNode(vm.toString(value), element));
        }
        children.push_back(element);
        return;
    }
    // The first takes the value; the others of that name go.
    const size_t at = static_cast<size_t>(first - children.begin());
    if (isXml && content.size() == 1 && content[0]->kind == XmlNode::Kind::Element) {
        children[at] = content[0];
    } else {
        children[at]->children.clear();
        if (isXml) {
            for (auto& item : content) item->parent = children[at];
            children[at]->children = content;
        } else {
            children[at]->children.push_back(textNode(vm.toString(value), children[at]));
        }
    }
    for (size_t i = children.size(); i-- > at + 1;) {
        if (children[i]->kind == XmlNode::Kind::Element && children[i]->name == name) {
            children.erase(children.begin() + static_cast<long>(i));
        }
    }
}

void removeChildren(const NodePtr& node, const std::string& name) {
    if (node->kind != XmlNode::Kind::Element) return;
    if (!name.empty() && name[0] == '@') {
        const std::string attribute = name.substr(1);
        auto& attributes = node->attributes;
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                                        [&attribute](const std::pair<std::string, std::string>& pair) {
                                            return attribute == "*" || pair.first == attribute;
                                        }),
                         attributes.end());
        return;
    }
    auto& children = node->children;
    children.erase(std::remove_if(children.begin(), children.end(),
                                  [&name](const NodePtr& child) {
                                      return name == "*" || (child->kind == XmlNode::Kind::Element && child->name == name);
                                  }),
                   children.end());
}

bool isIndex(const Val& key, size_t& index) {
    if (key.isNumber()) {
        if (key.num < 0 || key.num != std::floor(key.num)) return false;
        index = static_cast<size_t>(key.num);
        return true;
    }
    return false;
}

// An XML: one node.
struct XmlHost : HostObject {
    VM& vm;
    NodePtr node;
    XmlHost(VM& machine, NodePtr n) : vm(machine), node(std::move(n)) {}

    ValuePtr get(const std::string& name) override {
        return makeList(vm, pick(node, name), node, name);
    }
    bool set(const std::string& name, ValuePtr value) override {
        putChild(vm, node, name, Val(value));
        return true;
    }
    bool getIndex(const Val& key, Val& out) override {
        size_t index;
        if (isIndex(key, index)) {
            out = index == 0 ? makeXml(vm, node) : Val();
            return true;
        }
        out = makeList(vm, pick(node, vm.toString(key)), node, vm.toString(key));
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        size_t index;
        if (isIndex(key, index)) return true;
        putChild(vm, node, vm.toString(key), value);
        return true;
    }
    bool deleteIndex(const Val& key) override {
        removeChildren(node, key.isString() ? key.str() : vm.toString(key));
        return true;
    }
    bool hasIndex(const Val& key) override {
        size_t index;
        if (isIndex(key, index)) return index == 0;
        const std::string name = key.isString() ? key.str() : vm.toString(key);
        // An attribute is always the item's (in a filter: (@id == 3)).
        if (!name.empty() && name[0] == '@') return true;
        return !pick(node, name).empty();
    }
    bool enumerate(std::vector<Val>& keys) override {
        keys.push_back(Val::number(0));
        return true;
    }
};

// An XMLList: nodes, and where an assignment past its end puts a new one:
// the element its nodes are children of, or (for x.a.b when x has no a)
// the list it came from, whose element is made first.
struct XmlListHost : HostObject, std::enable_shared_from_this<XmlListHost> {
    VM& vm;
    std::vector<NodePtr> nodes;
    NodePtr target;
    std::string targetName;
    std::shared_ptr<XmlListHost> from;
    XmlListHost(VM& machine, std::vector<NodePtr> n) : vm(machine), nodes(std::move(n)) {}

    // The element new nodes of this list go in.
    NodePtr container() {
        if (target) return target;
        if (from) {
            if (from->nodes.empty()) from->makeOne();
            if (from->nodes.size() == 1) target = from->nodes[0];
        }
        return target;
    }
    // An element of the list's name, made in its container.
    void makeOne() {
        if (targetName.empty() || targetName == "*" || targetName[0] == '@') return;
        NodePtr where = container();
        if (!where) return;
        auto element = std::make_shared<XmlNode>();
        element->name = targetName;
        element->parent = where;
        where->children.push_back(element);
        nodes.push_back(element);
    }

    ValuePtr get(const std::string& name) override {
        std::vector<NodePtr> found;
        for (const auto& node : nodes) {
            for (const auto& item : pick(node, name)) found.push_back(item);
        }
        Val list = makeList(vm, std::move(found), nodes.size() == 1 ? nodes[0] : nullptr, name);
        list.host<XmlListHost>()->from = shared_from_this();
        return list;
    }
    bool set(const std::string& name, ValuePtr value) override {
        if (nodes.empty()) {
            // list.name = v on an empty list: its element made first.
            makeOne();
        }
        if (nodes.size() == 1) {
            putChild(vm, nodes[0], name, Val(value));
        }
        return true;
    }
    bool getIndex(const Val& key, Val& out) override {
        size_t index;
        if (isIndex(key, index)) {
            out = index < nodes.size() ? makeXml(vm, nodes[index]) : Val();
            return true;
        }
        Val list(get(vm.toString(key)));
        out = list;
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        size_t index;
        if (!isIndex(key, index)) {
            set(vm.toString(key), value);
            return true;
        }
        const bool isXml = value.isObject() && (value.host<XmlHost>() || value.host<XmlListHost>());
        if (index < nodes.size()) {
            NodePtr& node = nodes[index];
            if (node->kind == XmlNode::Kind::Attribute) {
                if (NodePtr element = node->parent.lock()) {
                    for (auto& pair : element->attributes) {
                        if (pair.first == node->name) pair.second = vm.toString(value);
                    }
                }
                node->value = vm.toString(value);
                return true;
            }
            node->children.clear();
            if (isXml) {
                for (const auto& item : nodesOf(vm, value)) node->children.push_back(copyNode(*item, node));
            } else {
                node->children.push_back(textNode(vm.toString(value), node));
            }
            return true;
        }
        NodePtr where = container();
        if (where && !targetName.empty() && targetName != "*" && targetName[0] != '@') {
            // Past the end: a new element of the list's name.
            auto element = std::make_shared<XmlNode>();
            element->name = targetName;
            element->parent = where;
            if (isXml) {
                for (const auto& item : nodesOf(vm, value)) element->children.push_back(copyNode(*item, element));
            } else {
                element->children.push_back(textNode(vm.toString(value), element));
            }
            where->children.push_back(element);
            nodes.push_back(element);
        }
        return true;
    }
    bool deleteIndex(const Val& key) override {
        size_t index;
        if (isIndex(key, index)) {
            if (index < nodes.size()) {
                NodePtr node = nodes[index];
                if (NodePtr parent = node->parent.lock()) {
                    if (node->kind == XmlNode::Kind::Attribute) {
                        auto& attributes = parent->attributes;
                        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                                                        [&node](const std::pair<std::string, std::string>& pair) {
                                                            return pair.first == node->name;
                                                        }),
                                         attributes.end());
                    } else {
                        auto& siblings = parent->children;
                        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
                    }
                }
                nodes.erase(nodes.begin() + static_cast<long>(index));
            }
            return true;
        }
        for (const auto& node : nodes) removeChildren(node, vm.toString(key));
        return true;
    }
    bool hasIndex(const Val& key) override {
        size_t index;
        if (isIndex(key, index)) return index < nodes.size();
        const std::string name = key.isString() ? key.str() : vm.toString(key);
        if (!name.empty() && name[0] == '@') return true;
        for (const auto& node : nodes) {
            if (!pick(node, name).empty()) return true;
        }
        return false;
    }
    bool enumerate(std::vector<Val>& keys) override {
        for (size_t i = 0; i < nodes.size(); ++i) keys.push_back(Val::number(static_cast<double>(i)));
        return true;
    }
};

Val makeXml(VM& vm, NodePtr node) {
    Val made = vm.newInstance(vm.findClass("XML"));
    made->host = std::make_shared<XmlHost>(vm, std::move(node));
    return made;
}

Val makeList(VM& vm, std::vector<NodePtr> nodes, NodePtr target, const std::string& targetName) {
    Val made = vm.newInstance(vm.findClass("XMLList"));
    auto host = std::make_shared<XmlListHost>(vm, std::move(nodes));
    host->target = std::move(target);
    host->targetName = targetName;
    made->host = host;
    return made;
}

// The nodes of an XML (one) or XMLList.
std::vector<NodePtr> nodesOf(VM& vm, const Val& value) {
    if (const XmlHost* xml = value.host<XmlHost>()) return {xml->node};
    if (const XmlListHost* list = value.host<XmlListHost>()) return list->nodes;
    (void)vm;
    return {};
}

// Nodes from a value: XML as it is, a string parsed.
std::vector<NodePtr> parseValue(VM& vm, const Val& value) {
    if (value.host<XmlHost>() || value.host<XmlListHost>()) {
        std::vector<NodePtr> copies;
        for (const auto& node : nodesOf(vm, value)) copies.push_back(copyNode(*node, NodePtr()));
        return copies;
    }
    if (value.isNullish()) return {};
    const std::string text = vm.toString(value);
    return XmlReader(vm, text).readAll();
}

NodePtr parseOne(VM& vm, const Val& value) {
    std::vector<NodePtr> nodes = parseValue(vm, value);
    if (nodes.empty()) return textNode(std::string(), nullptr);
    if (nodes.size() > 1) {
        vm.throwError("TypeError",
                      "Error #1088: The markup in the document following the root element must be well-formed.", 1088);
    }
    nodes[0]->parent.reset();
    return nodes[0];
}

std::string listText(VM& vm, const std::vector<NodePtr>& nodes, bool markup) {
    (void)vm;
    bool simple = true;
    for (const auto& node : nodes) {
        if (!hasSimpleContent(*node)) simple = false;
    }
    std::string out;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!markup && simple) {
            out += textOf(*nodes[i]);
        } else {
            if (i > 0) out += "\n";
            out += toXmlString(*nodes[i]);
        }
    }
    return out;
}

const char* kindName(XmlNode::Kind kind) {
    switch (kind) {
        case XmlNode::Kind::Element: return "element";
        case XmlNode::Kind::Text: return "text";
        case XmlNode::Kind::Comment: return "comment";
        case XmlNode::Kind::Instruction: return "processing-instruction";
        case XmlNode::Kind::Attribute: return "attribute";
    }
    return "element";
}

// The methods XML and XMLList share, on the nodes of either.
using Nodes = std::vector<NodePtr>;

void sharedMethods(NativeClassBuilder& builder) {
    const auto onNodes = [](std::function<Val(VM&, const Val&, const Nodes&, Args)> body) {
        return [body](VM& vm, const Val& self, Args args) { return body(vm, self, nodesOf(vm, self), args); };
    };
    builder
        .method("toString", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            return Val::string(listText(vm, nodes, false));
        }))
        .method("toXMLString", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            return Val::string(listText(vm, nodes, true));
        }))
        .method("valueOf", [](VM&, const Val& self, Args) { return self; })
        .method("length", onNodes([](VM&, const Val&, const Nodes& nodes, Args) {
            return Val::number(static_cast<double>(nodes.size()));
        }))
        .method("children", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            Nodes found;
            for (const auto& node : nodes) for (const auto& item : pick(node, "*")) found.push_back(item);
            return makeList(vm, found, nodes.size() == 1 ? nodes[0] : nullptr, "*");
        }))
        .method("child", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args args) {
            Nodes found;
            size_t index;
            if (isIndex(args[0], index)) {
                for (const auto& node : nodes) {
                    const Nodes all = pick(node, "*");
                    if (index < all.size()) found.push_back(all[index]);
                }
                return makeList(vm, found);
            }
            const std::string name = vm.toString(args[0]);
            for (const auto& node : nodes) for (const auto& item : pick(node, name)) found.push_back(item);
            return makeList(vm, found, nodes.size() == 1 ? nodes[0] : nullptr, name);
        }), 1)
        .method("elements", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args args) {
            const std::string name = args.empty() || args[0].isUndefined() ? "*" : vm.toString(args[0]);
            Nodes found;
            for (const auto& node : nodes) {
                for (const auto& item : pick(node, name)) {
                    if (item->kind == XmlNode::Kind::Element) found.push_back(item);
                }
            }
            return makeList(vm, found);
        }), 1)
        .method("attributes", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            Nodes found;
            for (const auto& node : nodes) for (const auto& item : pick(node, "@*")) found.push_back(item);
            return makeList(vm, found);
        }))
        .method("attribute", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args args) {
            const std::string name = "@" + vm.toString(args[0]);
            Nodes found;
            for (const auto& node : nodes) for (const auto& item : pick(node, name)) found.push_back(item);
            return makeList(vm, found);
        }), 1)
        .method("descendants", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args args) {
            const std::string name = args.empty() || args[0].isUndefined() ? "*" : vm.toString(args[0]);
            Nodes found;
            for (const auto& node : nodes) descendants(node, name, found);
            return makeList(vm, found);
        }), 1)
        .method("text", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            Nodes found;
            for (const auto& node : nodes) {
                for (const auto& child : node->children) {
                    if (child->kind == XmlNode::Kind::Text) found.push_back(child);
                }
            }
            return makeList(vm, found);
        }))
        .method("comments", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            Nodes found;
            for (const auto& node : nodes) {
                for (const auto& child : node->children) {
                    if (child->kind == XmlNode::Kind::Comment) found.push_back(child);
                }
            }
            return makeList(vm, found);
        }))
        .method("hasSimpleContent", onNodes([](VM&, const Val&, const Nodes& nodes, Args) {
            for (const auto& node : nodes) {
                if (!hasSimpleContent(*node)) return Val::boolean(false);
            }
            return Val::boolean(true);
        }))
        .method("hasComplexContent", onNodes([](VM&, const Val&, const Nodes& nodes, Args) {
            for (const auto& node : nodes) {
                if (node->kind == XmlNode::Kind::Element && !hasSimpleContent(*node)) return Val::boolean(true);
            }
            return Val::boolean(false);
        }))
        .method("copy", onNodes([](VM& vm, const Val& self, const Nodes& nodes, Args) {
            Nodes copies;
            for (const auto& node : nodes) copies.push_back(copyNode(*node, NodePtr()));
            if (self.host<XmlHost>()) return makeXml(vm, copies[0]);
            return makeList(vm, copies);
        }))
        .method("parent", onNodes([](VM& vm, const Val&, const Nodes& nodes, Args) {
            NodePtr parent = nodes.empty() ? nullptr : nodes[0]->parent.lock();
            for (const auto& node : nodes) {
                if (node->parent.lock() != parent) return Val();
            }
            if (!parent) return Val();
            return makeXml(vm, parent);
        }));
}

} // namespace

void defineXmlBuiltins(VM& vm) {
    // XML: a node. Its children are its properties (x.item); its methods
    // are found when called.
    NativeClassBuilder xml = vm.defineNativeClass("XML");
    xml.final()
        .dynamic()
        .constructor([](VM& vm, const Val& self, Args args) {
            self->host = std::make_shared<XmlHost>(vm, parseOne(vm, args[0]));
            return Val();
        })
        .callHandler([](VM& vm, const Val&, Args args) {
            if (args[0].host<XmlHost>()) return args[0];
            if (const XmlListHost* list = args[0].host<XmlListHost>()) {
                if (list->nodes.size() == 1) return makeXml(vm, list->nodes[0]);
            }
            return makeXml(vm, parseOne(vm, args[0]));
        })
        .staticGetter("ignoreComments", [](VM&, const Val&, Args) { return Val::boolean(settings().ignoreComments); })
        .staticSetter("ignoreComments", [](VM&, const Val&, Args args) {
            settings().ignoreComments = args[0].toBoolean();
            return Val();
        })
        .staticGetter("ignoreProcessingInstructions",
                      [](VM&, const Val&, Args) { return Val::boolean(settings().ignoreProcessingInstructions); })
        .staticSetter("ignoreProcessingInstructions", [](VM&, const Val&, Args args) {
            settings().ignoreProcessingInstructions = args[0].toBoolean();
            return Val();
        })
        .staticGetter("ignoreWhitespace", [](VM&, const Val&, Args) { return Val::boolean(settings().ignoreWhitespace); })
        .staticSetter("ignoreWhitespace", [](VM&, const Val&, Args args) {
            settings().ignoreWhitespace = args[0].toBoolean();
            return Val();
        })
        .staticGetter("prettyPrinting", [](VM&, const Val&, Args) { return Val::boolean(settings().prettyPrinting); })
        .staticSetter("prettyPrinting", [](VM&, const Val&, Args args) {
            settings().prettyPrinting = args[0].toBoolean();
            return Val();
        })
        .staticGetter("prettyIndent", [](VM&, const Val&, Args) { return Val::number(settings().prettyIndent); })
        .staticSetter("prettyIndent", [](VM&, const Val&, Args args) {
            settings().prettyIndent = std::max(0, args[0].toInt32());
            return Val();
        })
        // The five settings as an object, to keep and put back.
        .staticMethod("settings", [](VM& vm, const Val&, Args) { return settingsObject(vm, settings()); })
        .staticMethod("defaultSettings", [](VM& vm, const Val&, Args) { return settingsObject(vm, XmlSettings()); })
        .staticMethod("setSettings", [](VM& vm, const Val&, Args args) {
            XmlSettings made;
            if (args[0].isObject()) {
                const Val& from = args[0];
                const auto flag = [&](const char* name, bool& to) {
                    if (vm.hasProperty(from, name)) to = vm.getProperty(from, name).toBoolean();
                };
                flag("ignoreComments", made.ignoreComments);
                flag("ignoreProcessingInstructions", made.ignoreProcessingInstructions);
                flag("ignoreWhitespace", made.ignoreWhitespace);
                flag("prettyPrinting", made.prettyPrinting);
                if (vm.hasProperty(from, "prettyIndent")) made.prettyIndent = vm.getProperty(from, "prettyIndent").toInt32();
            }
            settings() = made;
            return Val();
        })
        .method("name", [](VM&, const Val& self, Args) {
            const XmlNode& node = *self.host<XmlHost>()->node;
            return node.kind == XmlNode::Kind::Element || node.kind == XmlNode::Kind::Attribute ||
                           node.kind == XmlNode::Kind::Instruction
                       ? Val::string(node.name)
                       : Val::null();
        })
        .method("localName", [](VM&, const Val& self, Args) {
            const XmlNode& node = *self.host<XmlHost>()->node;
            const size_t colon = node.name.find(':');
            return Val::string(colon == std::string::npos ? node.name : node.name.substr(colon + 1));
        })
        .method("setName", [](VM& vm, const Val& self, Args args) {
            self.host<XmlHost>()->node->name = vm.toString(args[0]);
            return Val();
        }, 1)
        .method("nodeKind", [](VM&, const Val& self, Args) {
            return Val::string(kindName(self.host<XmlHost>()->node->kind));
        })
        .method("childIndex", [](VM&, const Val& self, Args) {
            const XmlNode* node = self.host<XmlHost>()->node.get();
            NodePtr parent = node->parent.lock();
            if (!parent) return Val::number(-1);
            const auto& siblings = parent->children;
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (siblings[i].get() == node) return Val::number(static_cast<double>(i));
            }
            return Val::number(-1);
        })
        .method("appendChild", [](VM& vm, const Val& self, Args args) {
            const NodePtr& node = self.host<XmlHost>()->node;
            for (const auto& item : parseValue(vm, args[0])) {
                item->parent = node;
                node->children.push_back(item);
            }
            return self;
        }, 1)
        .method("prependChild", [](VM& vm, const Val& self, Args args) {
            const NodePtr& node = self.host<XmlHost>()->node;
            auto items = parseValue(vm, args[0]);
            for (auto& item : items) item->parent = node;
            node->children.insert(node->children.begin(), items.begin(), items.end());
            return self;
        }, 1)
        .method("insertChildAfter", [](VM& vm, const Val& self, Args args) {
            const NodePtr& node = self.host<XmlHost>()->node;
            const auto refs = nodesOf(vm, args[0]);
            auto items = parseValue(vm, args[1]);
            for (auto& item : items) item->parent = node;
            auto at = refs.empty() ? node->children.begin()
                                   : std::find(node->children.begin(), node->children.end(), refs[0]);
            if (!refs.empty() && at == node->children.end()) return Val();
            node->children.insert(refs.empty() ? at : at + 1, items.begin(), items.end());
            return self;
        }, 2)
        .method("insertChildBefore", [](VM& vm, const Val& self, Args args) {
            const NodePtr& node = self.host<XmlHost>()->node;
            const auto refs = nodesOf(vm, args[0]);
            auto items = parseValue(vm, args[1]);
            for (auto& item : items) item->parent = node;
            auto at = refs.empty() ? node->children.end()
                                   : std::find(node->children.begin(), node->children.end(), refs[0]);
            if (!refs.empty() && at == node->children.end()) return Val();
            node->children.insert(at, items.begin(), items.end());
            return self;
        }, 2)
        .method("setChildren", [](VM& vm, const Val& self, Args args) {
            putChild(vm, self.host<XmlHost>()->node, "*", args[0]);
            return self;
        }, 1)
        .method("replace", [](VM& vm, const Val& self, Args args) {
            const NodePtr& node = self.host<XmlHost>()->node;
            size_t index;
            if (isIndex(args[0], index)) {
                if (index < node->children.size()) {
                    auto items = parseValue(vm, args[1]);
                    if (items.empty()) items.push_back(textNode(vm.toString(args[1]), node));
                    for (auto& item : items) item->parent = node;
                    node->children.erase(node->children.begin() + static_cast<long>(index));
                    node->children.insert(node->children.begin() + static_cast<long>(index), items.begin(), items.end());
                }
                return self;
            }
            putChild(vm, node, vm.toString(args[0]), args[1]);
            return self;
        }, 2);
    sharedMethods(xml);
    xml.define();

    NativeClassBuilder list = vm.defineNativeClass("XMLList");
    list.final()
        .dynamic()
        .constructor([](VM& vm, const Val& self, Args args) {
            self->host = std::make_shared<XmlListHost>(vm, parseValue(vm, args[0]));
            return Val();
        })
        .callHandler([](VM& vm, const Val&, Args args) {
            if (args[0].host<XmlListHost>()) return args[0];
            return makeList(vm, parseValue(vm, args[0]));
        })
        .method("contains", [](VM& vm, const Val& self, Args args) {
            const std::string wanted = vm.toString(args[0]);
            for (const auto& node : self.host<XmlListHost>()->nodes) {
                if (toXmlString(*node) == wanted || textOf(*node) == wanted) return Val::boolean(true);
            }
            return Val::boolean(false);
        }, 1);
    sharedMethods(list);
    list.define();

    // Names in XML read children before methods; methods win when called.
    vm.findClass("XML")->hostFirst = true;
    vm.findClass("XMLList")->hostFirst = true;

    // describeType: a class's members as XML, as flash.utils has it.
    vm.defineFunction("flash.utils.describeType", [](VM& vm, const Val&, Args args) {
        const Val& value = args[0];
        const auto qualified = [](const ClassInfo* cls) {
            if (!cls) return std::string("null");
            return cls->packageName.empty() ? cls->name : cls->packageName + "::" + cls->shortName;
        };
        const auto attr = [](const std::string& text) { return escapeAttribute(text); };
        const auto members = [&](const std::unordered_map<std::string, Trait>& traits) {
            std::vector<std::string> names;
            for (const auto& entry : traits) {
                if (entry.first.empty() || entry.first[0] == '$' || entry.first.find("::") != std::string::npos ||
                    !entry.second.isPublic) {
                    continue;
                }
                names.push_back(entry.first);
            }
            std::sort(names.begin(), names.end());
            std::string out;
            for (const auto& name : names) {
                const Trait& trait = traits.at(name);
                const std::string type = trait.typeName.empty() ? "*" : trait.typeName;
                const std::string declaredBy = qualified(trait.declaredBy);
                std::string inner;
                if (trait.metadata) {
                    for (const auto& tag : *trait.metadata) {
                        inner += "<metadata name=\"" + attr(tag.name) + "\">";
                        for (const auto& argument : tag.arguments) {
                            inner += "<arg key=\"" + attr(argument.first) + "\" value=\"" + attr(argument.second) + "\"/>";
                        }
                        inner += "</metadata>";
                    }
                }
                std::string element;
                switch (trait.kind) {
                    case TraitKind::Slot:
                        element = std::string(trait.isConst ? "constant" : "variable") + " name=\"" + attr(name) +
                                  "\" type=\"" + attr(type) + "\"";
                        break;
                    case TraitKind::Accessor: {
                        const char* access = trait.getter.isObject() && trait.setter.isObject() ? "readwrite"
                                             : trait.getter.isObject()                             ? "readonly"
                                                                                                    : "writeonly";
                        element = "accessor name=\"" + attr(name) + "\" access=\"" + access + "\" type=\"" +
                                  attr(type) + "\" declaredBy=\"" + attr(declaredBy) + "\"";
                        break;
                    }
                    case TraitKind::Method:
                        element = "method name=\"" + attr(name) + "\" declaredBy=\"" + attr(declaredBy) +
                                  "\" returnType=\"*\"";
                        break;
                }
                const std::string tag = element.substr(0, element.find(' '));
                out += inner.empty() ? "<" + element + "/>" : "<" + element + ">" + inner + "</" + tag + ">";
            }
            return out;
        };
        const auto heritage = [&](const ClassInfo* cls) {
            std::string out;
            for (const ClassInfo* at = cls ? cls->base.get() : nullptr; at; at = at->base.get()) {
                out += "<extendsClass type=\"" + attr(qualified(at)) + "\"/>";
            }
            for (const ClassInfo* at = cls; at; at = at->base.get()) {
                for (const auto& face : at->interfaces) {
                    out += "<implementsInterface type=\"" + attr(qualified(face.get())) + "\"/>";
                }
            }
            return out;
        };
        std::string text;
        if (value.isClass()) {
            const ClassInfo* cls = value->cls.get();
            text = "<type name=\"" + attr(qualified(cls)) + "\" base=\"Class\" isDynamic=\"true\" isFinal=\"true\" "
                   "isStatic=\"true\"><extendsClass type=\"Class\"/><extendsClass type=\"Object\"/>" +
                   members(cls->statics) + "<factory type=\"" + attr(qualified(cls)) + "\">" + heritage(cls) +
                   members(cls->traits) + "</factory></type>";
        } else {
            const ClassInfo* cls = vm.classOf(value);
            const std::string name = value.isNullish() ? (value.isNull() ? "null" : "void") : qualified(cls);
            text = "<type name=\"" + attr(name) + "\"";
            if (cls && cls->base) text += " base=\"" + attr(qualified(cls->base.get())) + "\"";
            text += std::string(" isDynamic=\"") + (cls && cls->isDynamic ? "true" : "false") + "\" isFinal=\"" +
                    (cls && cls->isFinal ? "true" : "false") + "\" isStatic=\"false\">" + heritage(cls) +
                    (cls ? members(cls->traits) : std::string()) + "</type>";
        }
        return makeXml(vm, parseOne(vm, Val::string(text)));
    }, 1);

    // What compiled XML literals and filters call.
    vm.defineFunction("$xmlPart", [](VM& vm, const Val&, Args args) {
        const std::string text = vm.toString(args[0]);
        switch (args.integer(1)) {
            case 1: return Val::string(args[0].host<XmlHost>() || args[0].host<XmlListHost>()
                                           ? listText(vm, nodesOf(vm, args[0]), true)
                                           : escapeText(text));
            case 2: return Val::string("\"" + escapeAttribute(text) + "\"");
            default: return Val::string(text);
        }
    }, 2);
    vm.defineFunction("$xmlFilter", [](VM& vm, const Val&, Args args) {
        Nodes kept;
        for (const auto& node : nodesOf(vm, args[0])) {
            Val item = makeXml(vm, node);
            if (vm.callFunction(args[1], Val(), {item}).toBoolean()) kept.push_back(node);
        }
        return makeList(vm, kept);
    }, 2);
}

} // namespace as3
