#include "test_runner.h"
#include "swf_reader.h"
#include "../src/abc/abc_file.h"
#include "../src/abc/abc_opcodes.h"
#include "../src/abc/abc_verifier.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// The abcFile reader, writer and verifier: files made by hand, and the ABC
// of a movie Flash CS5 published (bunnyPaint2.swf, skipped where it is
// not).
TEST_SUITE(Abc)

namespace {

const char* kMovie = "D:/Backup/hulabunny/bunnyPaint/bunnyPaint2.swf";

std::vector<uint8_t> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

abc::Multiname multinameOf(uint8_t kind, uint32_t ns, uint32_t name, uint32_t set = 0) {
    abc::Multiname entry;
    entry.kind = kind;
    entry.ns = ns;
    entry.name = name;
    entry.nsSet = set;
    return entry;
}

// A file with one script whose init is the given code.
abc::AbcFile scriptOf(const std::vector<uint8_t>& code, uint32_t maxStack, uint32_t maxScope = 1) {
    abc::AbcFile file;
    file.pool.strings.push_back("trace");
    file.pool.namespaces.push_back({abc::NS_Package, 0});
    file.pool.multinames.push_back(multinameOf(abc::MN_QName, 1, 1));
    file.methods.emplace_back();
    abc::ScriptInfo script;
    script.init = 0;
    file.scripts.push_back(script);
    abc::MethodBody body;
    body.method = 0;
    body.maxStack = maxStack;
    body.localCount = 1;
    body.initScopeDepth = 0;
    body.maxScopeDepth = maxScope;
    body.code = code;
    file.bodies.push_back(body);
    return file;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string all;
    for (const auto& line : lines) all += line + "\n";
    return all;
}

void expectProblem(const abc::AbcFile& file, const std::string& part) {
    const std::string problems = joined(abc::verify(file));
    if (problems.find(part) == std::string::npos) {
        throw std::runtime_error("expected \"" + part + "\" in: " + problems);
    }
}

} // namespace

static void init_abc_tests() {
    auto& suite = suite_Abc;

    suite.addTest("Write and read a file", []() {
        abc::AbcFile file = scriptOf({abc::OP_getlocal0, abc::OP_pushscope, abc::OP_findpropstrict, 1,
                                      abc::OP_pushstring, 1, abc::OP_callpropvoid, 1, 1, abc::OP_returnvoid}, 2);
        file.pool.ints.push_back(-5);
        file.pool.uints.push_back(0xFFFFFFFFu);
        file.pool.doubles.push_back(2.5);
        file.pool.nsSets.push_back({{1}});
        file.pool.multinames.push_back(multinameOf(abc::MN_Multiname, 0, 1, 1));
        file.pool.multinames.push_back(multinameOf(abc::MN_MultinameL, 0, 0, 1));
        abc::Multiname vector;
        vector.kind = abc::MN_TypeName;
        vector.base = 1;
        vector.parameters = {0};
        file.pool.multinames.push_back(vector);
        abc::MetadataInfo tag;
        tag.name = 1;
        tag.items = {{0, 1}, {1, 1}};
        file.metadata.push_back(tag);
        const std::vector<uint8_t> bytes = abc::write(file);
        abc::AbcFile again;
        std::string error;
        if (!abc::read(bytes, again, error)) throw std::runtime_error(error);
        if (abc::write(again) != bytes) throw std::runtime_error("written again, it differs");
        if (again.pool.ints[1] != -5 || again.pool.uints[1] != 0xFFFFFFFFu || again.pool.doubles[1] != 2.5) {
            throw std::runtime_error("numbers changed");
        }
        if (again.metadata.size() != 1 || again.metadata[0].items.size() != 2) throw std::runtime_error("metadata changed");
        const auto problems = abc::verify(again);
        if (!problems.empty()) throw std::runtime_error(joined(problems));
    });

    suite.addTest("Flags kept as read, with nothing after them", []() {
        // HAS_PARAM_NAMES on a method without parameters, HAS_OPTIONAL with
        // no defaults, a trait's metadata flag with no tags: as compilers
        // write them sometimes.
        abc::AbcFile file = scriptOf({abc::OP_returnvoid}, 0);
        file.methods[0].flags = abc::METHOD_HasParamNames | abc::METHOD_HasOptional | abc::METHOD_NeedRest;
        abc::Trait trait;
        trait.name = 1;
        trait.attributes = abc::ATTR_Metadata;
        file.scripts[0].traits.push_back(trait);
        const std::vector<uint8_t> bytes = abc::write(file);
        abc::AbcFile again;
        std::string error;
        if (!abc::read(bytes, again, error)) throw std::runtime_error(error);
        if (again.methods[0].flags != file.methods[0].flags) throw std::runtime_error("flags changed");
        if (abc::write(again) != bytes) throw std::runtime_error("written again, it differs");
    });

    suite.addTest("A truncated file is an error", []() {
        abc::AbcFile file = scriptOf({abc::OP_returnvoid}, 0);
        std::vector<uint8_t> bytes = abc::write(file);
        bytes.resize(bytes.size() - 3);
        abc::AbcFile again;
        std::string error;
        if (abc::read(bytes, again, error)) throw std::runtime_error("read a truncated file");
    });

    suite.addTest("Verifier: stack underflow and max_stack", []() {
        expectProblem(scriptOf({abc::OP_pop, abc::OP_returnvoid}, 1), "takes 1 from a stack of 0");
        expectProblem(scriptOf({abc::OP_pushnull, abc::OP_pushnull, abc::OP_pop, abc::OP_pop, abc::OP_returnvoid}, 1),
                      "past max_stack");
    });

    suite.addTest("Verifier: scopes", []() {
        expectProblem(scriptOf({abc::OP_popscope, abc::OP_returnvoid}, 0), "popscope with no scope");
        expectProblem(scriptOf({abc::OP_getlocal0, abc::OP_pushscope, abc::OP_getlocal0, abc::OP_pushscope,
                                abc::OP_returnvoid}, 1, 1),
                      "past max_scope_depth");
    });

    suite.addTest("Verifier: branches", []() {
        // A jump into the middle of pushbyte.
        expectProblem(scriptOf({abc::OP_jump, 1, 0, 0, abc::OP_pushbyte, 1, abc::OP_pop, abc::OP_returnvoid}, 1),
                      "not an instruction");
        // Two paths meeting with different stacks.
        expectProblem(scriptOf({abc::OP_pushtrue, abc::OP_iftrue, 1, 0, 0, abc::OP_pushnull, abc::OP_returnvoid}, 1),
                      "where another path has stack");
        // A loop whose head is not a label.
        expectProblem(scriptOf({abc::OP_nop, abc::OP_jump, 0xFB, 0xFF, 0xFF}, 0), "not a label");
        // Off the end.
        expectProblem(scriptOf({abc::OP_nop}, 0), "runs off its end");
    });

    suite.addTest("Verifier: bodies and pool indexes", []() {
        abc::AbcFile file = scriptOf({abc::OP_getlex, 9, abc::OP_pop, abc::OP_returnvoid}, 1);
        expectProblem(file, "bad multiname 9");
        file.bodies.clear();
        expectProblem(file, "no body");
    });

    suite.addTest("Metadata of a Flex library: its keys, then its values", []() {
        const char* swc = "D:/Backup/hulabunny/as3corelib-.93/lib/as3corelib.swc";
        if (!std::filesystem::exists(swc)) {
            std::cout << "    (library not found; skipped)" << std::endl;
            return;
        }
        const std::vector<uint8_t> library = zipEntry(readBytes(swc), "library.swf");
        size_t blocks = 0, items = 0;
        for (const SwfTag& tag : swfTags(library)) {
            if (tag.code != 82) continue;
            abc::AbcFile file;
            std::string error;
            const std::vector<uint8_t> bytes = doAbcBytes(tag);
            if (!abc::read(bytes, file, error)) throw std::runtime_error(error);
            if (abc::write(file) != bytes) throw std::runtime_error("written again, it differs");
            const auto problems = abc::verify(file);
            if (!problems.empty()) throw std::runtime_error(joined(problems));
            ++blocks;
            for (const abc::MetadataInfo& tagInfo : file.metadata) {
                if (file.pool.strings[tagInfo.name] != "__go_to_definition_help") continue;
                // [__go_to_definition_help(file="...", pos="123")]
                for (const auto& item : tagInfo.items) {
                    const std::string& key = file.pool.strings[item.first];
                    const std::string& value = file.pool.strings[item.second];
                    if (key == "pos" && value.find_first_not_of("0123456789") != std::string::npos) {
                        throw std::runtime_error("pos is \"" + value + "\"");
                    }
                    if (key != "file" && key != "pos") throw std::runtime_error("a key \"" + key + "\"");
                    ++items;
                }
            }
        }
        if (blocks < 50 || items == 0) throw std::runtime_error(std::to_string(blocks) + " blocks, " + std::to_string(items) + " items");
        std::cout << "    " << blocks << " ABC blocks" << std::endl;
    });

    suite.addTest("Read, verify and write again Flash CS5's ABC", []() {
        if (!std::filesystem::exists(kMovie)) {
            std::cout << "    (movie not found; skipped)" << std::endl;
            return;
        }
        // The movie is a preloader; the real one is in its second binary
        // data tag, a compressed SWF of its own.
        std::vector<SwfTag> outer = swfTags(readBytes(kMovie));
        std::vector<std::vector<uint8_t>> movies;
        for (const SwfTag& tag : outer) {
            if (tag.code == 87 && tag.data.size() > 6) movies.emplace_back(tag.data.begin() + 6, tag.data.end());
        }
        if (movies.size() < 2) throw std::runtime_error("no embedded movie");
        std::vector<uint8_t> bytes;
        for (const SwfTag& tag : swfTags(movies[1])) {
            if (tag.code == 82) bytes = doAbcBytes(tag);
        }
        if (bytes.size() < 200000) throw std::runtime_error("ABC of " + std::to_string(bytes.size()) + " bytes");
        abc::AbcFile file;
        std::string error;
        if (!abc::read(bytes, file, error)) throw std::runtime_error(error);
        if (abc::write(file) != bytes) throw std::runtime_error("written again, it differs");
        const auto problems = abc::verify(file);
        if (!problems.empty()) throw std::runtime_error(joined(problems));
        size_t instructions = 0;
        for (const abc::MethodBody& body : file.bodies) {
            std::vector<abc::Instruction> code;
            if (!abc::decodeAll(body.code, code, error)) throw std::runtime_error(error);
            instructions += code.size();
        }
        bool timeline = false;
        for (const abc::InstanceInfo& instance : file.instances) {
            if (abc::multinameName(file, instance.name) == "bunnyPaint2_fla:MainTimeline") timeline = true;
        }
        if (!timeline) throw std::runtime_error("no MainTimeline");
        if (abc::disassemble(file).find("callpropvoid addFrameScript 2") == std::string::npos) {
            throw std::runtime_error("disassembly lacks addFrameScript");
        }
        std::cout << "    " << file.instances.size() << " classes, " << file.bodies.size() << " bodies, "
                  << instructions << " instructions" << std::endl;
    });
}

static struct AbcTestInit {
    AbcTestInit() { init_abc_tests(); }
} abc_test_init;
