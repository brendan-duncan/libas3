#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// E4X: XML literals, XML and XMLList, and the operators on them.
TEST_SUITE(XML)

namespace {

std::string run(const std::string& code) {
    VM vm;
    std::string out;
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + vm.toString(args[i]);
        }
        out += "\n";
        return Val();
    });
    if (!vm.load(code, "xml.as")) {
        throw std::runtime_error("Error: " + vm.getError() + " (line " + std::to_string(vm.getErrorLine()) + ")\n" +
                                 vm.getErrorStack() + "\nOutput so far:\n" + out);
    }
    return out;
}

void expect(const std::string& got, const std::string& wanted) {
    if (got != wanted) {
        throw std::runtime_error("got \"" + got + "\", wanted \"" + wanted + "\"");
    }
}

} // namespace

static void init_xml_tests() {
    auto& suite = suite_XML;

    suite.addTest("Literals, children and attributes", []() {
        expect(run("var x:XML = <order id=\"7\"><item sku=\"a1\">Pen</item><item sku=\"b2\">Ink</item><note>soon</note></order>;\n"
                   "trace(x.item.length(), x.item[1], x.item[0].@sku, x.@id, x.note, x.item.@sku, typeof x, x.nodeKind());"),
               "2 Ink a1 7 soon a1b2 xml element\n");
    });

    suite.addTest("Expressions in literals", []() {
        expect(run("var n = 3; var name = 'box'; var text = 'a < b';\n"
                   "var x = <{name} size={n * 2} label=\"{n}\">{text}<i>{n + 1}</i></{name}>;\n"
                   "trace(x.name(), x.@size, x.@label, x.toXMLString());"),
               "box 6 {n} <box size=\"6\" label=\"{n}\">\n  a &lt; b\n  <i>4</i>\n</box>\n");
    });

    suite.addTest("Descendants, wildcards and filters", []() {
        expect(run("var x = <shop><dept name=\"art\"><item price=\"3\">Pen</item><item price=\"12\">Easel</item></dept>"
                   "<dept name=\"toys\"><item price=\"5\">Ball</item></dept></shop>;\n"
                   "trace(x..item.length(), x..@price.length(), x.dept.(@name == 'toys').item, x..item.(@price > 4).length());\n"
                   "var cheap = x..item.(Number(@price) < 10); trace(cheap.length(), cheap[1]);\n"
                   "trace(x.*.length(), x.dept[0].*.length(), x.dept.@*.length(), x.descendants('item')[2]);\n"
                   "var limit = 4; trace(x..item.(@price > limit).length());"),
               "3 3 Ball 2\n2 Ball\n2 2 2 Ball\n2\n");
    });

    suite.addTest("Changing XML", []() {
        expect(run("var x = <a><b>1</b></a>;\n"
                   "x.b = 2; x.c = 'new'; x.@id = 9; x.b.@kind = 'k';\n"
                   "x.appendChild(<d/>); x.prependChild(<first/>);\n"
                   "XML.prettyPrinting = false; trace(x.toXMLString());\n"
                   "delete x.c; delete x.@id; trace(x.toXMLString());\n"
                   "x.list.item[0] = 'one'; x.list.item[1] = 'two'; trace(x.list.item.length(), x.list.item[1]);"),
               "<a id=\"9\"><first/><b kind=\"k\">2</b><c>new</c><d/></a>\n<a><first/><b kind=\"k\">2</b><d/></a>\n2 two\n");
    });

    suite.addTest("Parsing, for each and conversions", []() {
        expect(run("var x = new XML('<root a=\"&lt;1&gt;\"><v>1</v><v>2</v><v>3</v><!-- skipped --></root>');\n"
                   "var total = 0; for each (var v:XML in x.v) total += Number(v);\n"
                   "trace(total, x.@a, x.v[2] == 3, x.v[2] == '3', String(x.v[0]), x.v.length(), x.children().length());\n"
                   "var list = new XMLList('<a>1</a><a>2</a>'); trace(list.length(), list[1], XML('<z>q</z>').text());\n"
                   "var err = ''; try { new XML('<a><b></a>'); } catch (e:TypeError) { err = e.errorID; } trace(err);\n"
                   "trace(x.v[0].parent().name(), x.v[1].childIndex(), x.hasComplexContent(), x.v[0].hasSimpleContent());"),
               "6 <1> true true 1 3 3\n2 2 q\n1085\nroot 1 true true\n");
    });

    suite.addTest("describeType", []() {
        expect(run("import flash.utils.describeType;\n"
                   "class Shape { public var w:Number; public const SIDES:int = 4; private var secret:int;\n"
                   "  [Bindable] public var tag:String;\n"
                   "  public function get area():Number { return 0; } public function draw():void {} }\n"
                   "var d:XML = describeType(new Shape());\n"
                   "trace(d.@name, d.@base, d.variable.length(), d.constant.@name, d.accessor.@access, d.method.@name, d.variable.(@name == 'tag').metadata.@name);\n"
                   "var c:XML = describeType(Shape); trace(c.@isStatic, c.factory.@type, c.factory.accessor.@name);"),
               "Shape Object 2 SIDES readonly draw Bindable\ntrue Shape area\n");
    });

    suite.addTest("as3corelib's JSON: E4X, describeType and metadata", []() {
        const std::string root = "D:/Backup/hulabunny/bunnyPaint/as3corelib-.93/src/";
        if (!std::ifstream(root + "com/adobe/serialization/json/JSONEncoder.as")) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        VM vm;
        std::string out;
        vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
            out += vm.toString(args[0]) + "\n";
            return Val();
        });
        vm.setClassResolver([root](const std::string& name) -> std::optional<ClassSource> {
            std::string path = name;
            for (char& c : path) if (c == '.') c = '/';
            std::ifstream in(root + path + ".as", std::ios::binary);
            if (!in) return std::nullopt;
            std::stringstream text;
            text << in.rdbuf();
            return ClassSource{text.str(), name + ".as"};
        });
        const bool loaded = vm.load(
            "import com.adobe.serialization.json.JSON;\n"
            "class Point3 { public var x:Number = 1; public var y:Number = 2; [Transient] public var cache:Object;\n"
            "  public function get len():Number { return 3; } private var hidden:int = 4; }\n"
            "trace(JSON.encode({list: [1, 'two', true, null], nested: {k: 'v'}}));\n"
            "trace(JSON.encode(new Point3()));\n"
            "var back = JSON.decode('{\"k\":[1,2,{\"z\":\"q\\\\n\"}],\"n\":-1.5e2}'); trace(back.k[2].z + back.n);\n",
            "json.as");
        if (!loaded) throw std::runtime_error(vm.getError() + "\n" + vm.getErrorStack());
        // Object keys come in the order the object keeps them.
        expect(out, "{\"list\":[1,\"two\",true,null],\"nested\":{\"k\":\"v\"}}\n{\"len\":3,\"x\":1,\"y\":2}\nq\n-150\n");
    });

    suite.addTest("Comparisons and < still read as before", []() {
        expect(run("var a = 1, b = 2; var v:Vector.<int> = new <int>[1]; var c = a<b; var d = (a)<b;\n"
                   "if (a <b) trace(c, d, v.length < 2, a < b ? 'lt' : 'ge');"),
               "true true true lt\n");
    });
}

static struct XmlTestInit {
    XmlTestInit() { init_xml_tests(); }
} xml_test_init;
