#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// Classes the host defines and scripts use and extend.
TEST_SUITE(NativeClasses)

namespace {

std::string run(VM& vm, const std::string& code) {
    std::string out;
    vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
        for (size_t i = 0; i < args.size(); ++i) {
            out += (i ? " " : "") + vm.toString(args[i]);
        }
        out += "\n";
        return Val();
    });
    if (!vm.load(code, "test.as")) {
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

// flash.geom.Rectangle, with its state in C++.
struct RectState : HostObject {
    double x = 0, y = 0, width = 0, height = 0;
};

RectState& rect(VM& vm, const Val& self) {
    RectState* state = self.host<RectState>();
    if (!state) vm.throwError("TypeError", "Error #1034: Type Coercion failed: Rectangle expected.", 1034);
    return *state;
}

void defineRectangle(VM& vm) {
    const auto number = [](double RectState::*field) {
        return [field](VM& vm, const Val& self, Args) { return Val::number(rect(vm, self).*field); };
    };
    const auto setNumber = [](double RectState::*field) {
        return [field](VM& vm, const Val& self, Args args) {
            rect(vm, self).*field = vm.toNumber(args[0]);
            return Val();
        };
    };
    vm.defineNativeClass("flash.geom.Rectangle")
        .constructor([](VM& vm, const Val& self, Args args) {
            auto state = std::make_shared<RectState>();
            state->x = args.number(0);
            state->y = args.number(1);
            state->width = args.number(2);
            state->height = args.number(3);
            self->host = state;
            return Val();
        })
        .property("x", number(&RectState::x), setNumber(&RectState::x))
        .property("y", number(&RectState::y), setNumber(&RectState::y))
        .property("width", number(&RectState::width), setNumber(&RectState::width))
        .property("height", number(&RectState::height), setNumber(&RectState::height))
        .property("left", number(&RectState::x),
                  [](VM& vm, const Val& self, Args args) {
                      RectState& r = rect(vm, self);
                      const double left = vm.toNumber(args[0]);
                      r.width += r.x - left;
                      r.x = left;
                      return Val();
                  })
        .property("top", number(&RectState::y),
                  [](VM& vm, const Val& self, Args args) {
                      RectState& r = rect(vm, self);
                      const double top = vm.toNumber(args[0]);
                      r.height += r.y - top;
                      r.y = top;
                      return Val();
                  })
        .getter("right", [](VM& vm, const Val& self, Args) {
            RectState& r = rect(vm, self);
            return Val::number(r.x + r.width);
        })
        .getter("bottom", [](VM& vm, const Val& self, Args) {
            RectState& r = rect(vm, self);
            return Val::number(r.y + r.height);
        })
        .method("isEmpty", [](VM& vm, const Val& self, Args) {
            RectState& r = rect(vm, self);
            return Val::boolean(r.width <= 0 || r.height <= 0);
        })
        // Calls a method a subclass may override: the host calling a script.
        .method("describe", [](VM& vm, const Val& self, Args) {
            return Val::string("<" + vm.toString(vm.callMethod(self, "toString")) + ">");
        })
        .method("toString", [](VM& vm, const Val& self, Args) {
            RectState& r = rect(vm, self);
            return Val::string("(x=" + numberToString(r.x) + ", y=" + numberToString(r.y) + ", w=" +
                               numberToString(r.width) + ", h=" + numberToString(r.height) + ")");
        })
        .define();
}

// flash.events.EventDispatcher: listeners kept by the host, which calls
// them back.
struct DispatcherState : HostObject {
    std::multimap<std::string, Val> listeners;
};

DispatcherState& hostOfDispatcher(VM& vm, const Val& self) {
    DispatcherState* state = self.host<DispatcherState>();
    if (!state) vm.throwError("TypeError", "Error #1034: Type Coercion failed: EventDispatcher expected.", 1034);
    return *state;
}

void defineEvents(VM& vm) {
    vm.defineNativeClass("flash.events.Event")
        .constant("CHANGE", Val::string("change"))
        .field("type", Val::null(), Coerce::String)
        .field("target")
        .constructor([](VM& vm, const Val& self, Args args) {
            vm.setProperty(self, "type", args[0]);
            return Val();
        })
        .define();
    vm.defineNativeClass("flash.events.EventDispatcher")
        .constructor([](VM&, const Val& self, Args) {
            // A host may have attached its own state before (newInstance).
            if (!self->host) self->host = std::make_shared<DispatcherState>();
            return Val();
        })
        .method("addEventListener",
                [](VM& vm, const Val& self, Args args) {
                    hostOfDispatcher(vm, self).listeners.emplace(vm.toString(args[0]), args[1]);
                    return Val();
                },
                2)
        .method("dispatchEvent",
                [](VM& vm, const Val& self, Args args) {
                    Val event = args[0];
                    vm.setProperty(event, "target", self);
                    const std::string type = vm.toString(vm.getProperty(event, "type"));
                    auto range = hostOfDispatcher(vm, self).listeners.equal_range(type);
                    std::vector<Val> listeners;
                    for (auto it = range.first; it != range.second; ++it) listeners.push_back(it->second);
                    for (const Val& listener : listeners) vm.callFunction(listener, Val::null(), {event});
                    return Val::boolean(!listeners.empty());
                },
                1)
        .define();
    vm.defineNativeClass("flash.display.DisplayObject")
        .extends("flash.events.EventDispatcher")
        .property("x", [](VM& vm, const Val& self, Args) { return vm.getProperty(self, "$x"); },
                  [](VM& vm, const Val& self, Args args) {
                      vm.setProperty(self, "$x", Val::number(vm.toNumber(args[0])));
                      return Val();
                  })
        .field("$x", Val::number(0))
        .define();
    vm.defineNativeClass("flash.display.Sprite").extends("flash.display.DisplayObject").dynamic().define();
}

// flash.display.BitmapData and flash.utils.ByteArray, as the JPEG encoder
// needs them.
struct BitmapState : HostObject {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

struct BytesState : HostObject {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    bool getIndex(const Val& key, Val& out) override {
        if (!key.isNumber()) return false;
        const size_t i = static_cast<size_t>(key.num);
        out = i < bytes.size() ? Val::number(bytes[i]) : Val();
        return true;
    }
    bool setIndex(const Val& key, const Val& value) override {
        if (!key.isNumber()) return false;
        const size_t i = static_cast<size_t>(key.num);
        if (i >= bytes.size()) bytes.resize(i + 1);
        bytes[i] = static_cast<uint8_t>(value.toInt32());
        return true;
    }
};

void defineBitmapAndBytes(VM& vm) {
    vm.defineNativeClass("flash.display.BitmapData")
        .constructor([](VM& vm, const Val& self, Args args) {
            auto state = std::make_shared<BitmapState>();
            state->width = args.integer(0);
            state->height = args.integer(1);
            const uint32_t fill = args.size() > 3 ? args[3].toUint32() : 0xFFFFFFFF;
            state->pixels.assign(static_cast<size_t>(state->width) * static_cast<size_t>(state->height), fill);
            self->host = state;
            (void)vm;
            return Val();
        })
        .getter("width", [](VM&, const Val& self, Args) { return Val::number(self.host<BitmapState>()->width); })
        .getter("height", [](VM&, const Val& self, Args) { return Val::number(self.host<BitmapState>()->height); })
        .method("getPixel32",
                [](VM&, const Val& self, Args args) {
                    BitmapState* bitmap = self.host<BitmapState>();
                    const int x = args.integer(0);
                    const int y = args.integer(1);
                    if (x < 0 || y < 0 || x >= bitmap->width || y >= bitmap->height) return Val::number(0);
                    return Val::number(bitmap->pixels[static_cast<size_t>(y * bitmap->width + x)]);
                },
                2)
        .method("getPixel",
                [](VM&, const Val& self, Args args) {
                    BitmapState* bitmap = self.host<BitmapState>();
                    const int x = args.integer(0);
                    const int y = args.integer(1);
                    if (x < 0 || y < 0 || x >= bitmap->width || y >= bitmap->height) return Val::number(0);
                    return Val::number(bitmap->pixels[static_cast<size_t>(y * bitmap->width + x)] & 0xFFFFFF);
                },
                2)
        .method("setPixel32",
                [](VM&, const Val& self, Args args) {
                    BitmapState* bitmap = self.host<BitmapState>();
                    const int x = args.integer(0);
                    const int y = args.integer(1);
                    if (x >= 0 && y >= 0 && x < bitmap->width && y < bitmap->height) {
                        bitmap->pixels[static_cast<size_t>(y * bitmap->width + x)] = args[2].toUint32();
                    }
                    return Val();
                },
                3)
        .define();
    vm.defineNativeClass("flash.utils.ByteArray")
        .constructor([](VM&, const Val& self, Args) {
            self->host = std::make_shared<BytesState>();
            return Val();
        })
        .method("writeByte",
                [](VM&, const Val& self, Args args) {
                    BytesState* bytes = self.host<BytesState>();
                    if (bytes->position >= bytes->bytes.size()) bytes->bytes.resize(bytes->position + 1);
                    bytes->bytes[bytes->position++] = static_cast<uint8_t>(args[0].toInt32());
                    return Val();
                },
                1)
        .property("length",
                  [](VM&, const Val& self, Args) { return Val::number(static_cast<double>(self.host<BytesState>()->bytes.size())); },
                  [](VM&, const Val& self, Args args) {
                      self.host<BytesState>()->bytes.resize(args[0].toUint32());
                      return Val();
                  })
        .property("position",
                  [](VM&, const Val& self, Args) { return Val::number(static_cast<double>(self.host<BytesState>()->position)); },
                  [](VM&, const Val& self, Args args) {
                      self.host<BytesState>()->position = args[0].toUint32();
                      return Val();
                  })
        .define();
}

} // namespace

static void init_native_class_tests() {
    auto& suite = suite_NativeClasses;

    suite.addTest("A script class extends a native one", []() {
        VM vm;
        defineRectangle(vm);
        // As bunnyPaint's BP_Rectangle: super() into the native
        // constructor, the native accessors and methods unqualified.
        expect(run(vm,
                   "package geo {\n"
                   "  import flash.geom.Rectangle;\n"
                   "  public final class Box extends Rectangle {\n"
                   "    public function Box(x:Number = 0, y:Number = 0, w:Number = 0, h:Number = 0) { super(x, y, w, h); }\n"
                   "    public function extend(x:Number, y:Number):void {\n"
                   "      if (isEmpty()) { left = x; top = y; width = 1; height = 1; return; }\n"
                   "      if (x < left) left = x;\n"
                   "      if (y < top) top = y;\n"
                   "      if (x > right) width = x - left;\n"
                   "      if (y > bottom) height = y - top;\n"
                   "    }\n"
                   "  }\n"
                   "}\n"
                   "import geo.Box;\n"
                   "import flash.geom.Rectangle;\n"
                   "var b:Box = new Box();\n"
                   "b.extend(5, 5); b.extend(2, 9); b.extend(10, 1);\n"
                   "trace(b.x, b.y, b.width, b.height, b.isEmpty(), b is Rectangle, b is Box, b instanceof Rectangle);\n"
                   "var plain = new Rectangle(1, 2, 3, 4); trace(plain.right, plain is Box, plain as Box);"),
               "2 1 8 8 false true true true\n4 false null\n");
        // The script instance keeps its native state.
        Val box = vm.getGlobal("b");
        RectState* state = box.host<RectState>();
        if (!state || state->width != 8) {
            throw std::runtime_error("native state not kept on the script instance");
        }
    });

    suite.addTest("Overriding native methods and accessors", []() {
        VM vm;
        defineRectangle(vm);
        expect(run(vm,
                   "import flash.geom.Rectangle;\n"
                   "class Named extends Rectangle {\n"
                   "  public function Named() { super(1, 1, 2, 2); }\n"
                   "  override public function toString():String { return 'Named' + super.toString(); }\n"
                   "  override public function get width():Number { return super.width * 100; }\n"
                   "  override public function isEmpty():Boolean { return true; }\n"
                   "}\n"
                   "var n = new Named();\n"
                   "trace(n.width, n.right, n.isEmpty());\n"
                   "trace(n.describe());"),
               "200 3 true\n<Named(x=1, y=1, w=2, h=2)>\n");
    });

    suite.addTest("The host calls script listeners with the right this", []() {
        VM vm;
        defineEvents(vm);
        expect(run(vm,
                   "package ui {\n"
                   "  import flash.display.Sprite;\n"
                   "  import flash.events.Event;\n"
                   "  public class Button extends Sprite {\n"
                   "    public var clicks:int = 0;\n"
                   "    public function Button() { addEventListener(Event.CHANGE, onChange); x = 5; }\n"
                   "    private function onChange(e:Event):void { clicks++; trace('changed', e.type, e.target == this, x); }\n"
                   "  }\n"
                   "}\n"
                   "import ui.Button;\n"
                   "import flash.events.*;\n"
                   "import flash.display.*;\n"
                   "var b:Button = new Button();\n"
                   "b.dispatchEvent(new Event(Event.CHANGE));\n"
                   "b.dispatchEvent(new Event('change'));\n"
                   "trace(b.clicks, b is DisplayObject, b is EventDispatcher, b is Sprite);"),
               "changed change true 5\nchanged change true 5\n2 true true true\n");
    });

    suite.addTest("The host makes an instance and attaches its state", []() {
        VM vm;
        defineEvents(vm);
        run(vm, "package { import flash.display.Sprite; public class Star extends Sprite { public var made:String = 'yes';"
                " public function Star() { super(); made += '!'; } } }");
        auto star = vm.findClass("Star");
        Val instance = vm.newInstance(star);
        auto state = std::make_shared<DispatcherState>();
        instance->host = state;
        vm.runConstructor(instance, Args());
        if (instance.host<DispatcherState>() != state.get() || vm.getProperty(instance, "made").toString() != "yes!") {
            throw std::runtime_error("host state replaced or constructor not run");
        }
    });

    suite.addTest("Errors from native methods are catchable", []() {
        VM vm;
        defineRectangle(vm);
        vm.defineFunction("explode", [](VM& vm, const Val&, Args args) -> Val {
            vm.throwError("ArgumentError", "Error #2004: " + args.string(0), 2004);
        });
        // A method read off an object stays bound to it, even with call.
        expect(run(vm,
                   "import flash.geom.Rectangle;\n"
                   "var r = new Rectangle(); var f = r.isEmpty; var s = '';\n"
                   "try { explode('bad'); } catch (e:ArgumentError) { s += e.errorID + ' ' + e.message; }\n"
                   "try { var t = new Rectangle(); t.isEmpty.call({}); s += ' bound'; } catch (e:TypeError) { s += ' type'; }\n"
                   "trace(s, f.call({}));"),
               "2004 Error #2004: bad bound true\n");
    });

    suite.addTest("Natives call back into scripts (sort, forEach)", []() {
        VM vm;
        expect(run(vm, "var a = [3, 1, 2]; a.sort(function(x, y) { return y - x; }); var s = 0; a.forEach(function(v) { s += v; }); trace(a, s);"),
               "3,2,1 6\n");
    });

    suite.addTest("JPGEncoder encodes a BitmapData", []() {
        namespace fs = std::filesystem;
        const fs::path images = "D:/Backup/hulabunny/bunnyPaint/as3corelib-.93/src/com/adobe/images";
        if (!fs::exists(images / "JPGEncoder.as")) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        VM vm;
        defineBitmapAndBytes(vm);
        vm.setClassResolver([images](const std::string& name) -> std::optional<ClassSource> {
            const std::string prefix = "com.adobe.images.";
            if (name.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
            const fs::path path = images / (name.substr(prefix.size()) + ".as");
            if (!fs::exists(path)) return std::nullopt;
            std::ifstream in(path, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return ClassSource{text.str(), path.filename().string()};
        });
        run(vm,
            "import com.adobe.images.JPGEncoder;\n"
            "import flash.display.BitmapData;\n"
            "var image:BitmapData = new BitmapData(20, 12, false, 0xFF336699);\n"
            "for (var y:int = 0; y < 12; y++) for (var x:int = 0; x < 20; x++) image.setPixel32(x, y, 0xFF000000 | (x * 12) << 16 | (y * 20) << 8 | 128);\n"
            "var jpeg = new JPGEncoder(80).encode(image);\n");
        Val jpeg = vm.getGlobal("jpeg");
        BytesState* bytes = jpeg.host<BytesState>();
        if (!bytes || bytes->bytes.size() < 100) {
            throw std::runtime_error("no JPEG made");
        }
        const auto& data = bytes->bytes;
        if (data[0] != 0xFF || data[1] != 0xD8 || data[data.size() - 2] != 0xFF || data[data.size() - 1] != 0xD9) {
            throw std::runtime_error("not a JPEG: starts and ends wrong");
        }
        std::cout << "    " << data.size() << " bytes" << std::endl;
    });
}

static struct NativeClassTestInit {
    NativeClassTestInit() { init_native_class_tests(); }
} native_class_test_init;
