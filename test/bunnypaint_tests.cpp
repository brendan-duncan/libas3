#include "test_runner.h"
#include "../src/parser/parser.h"
#include "../src/bytecode/compiler.h"
#include "../src/bytecode/vm.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace as3;
using namespace as3test;

// A real program: bunnyPaint, a paint program of 41 classes, and
// as3corelib's JPEG encoder, compiled and loaded through the class
// resolver against stand-ins for the flash.* and fl.* classes they use.
// Skipped where the sources are not there.
TEST_SUITE(BunnyPaint)

namespace {

namespace fs = std::filesystem;

const char* kRoot = "D:/Backup/hulabunny/bunnyPaint/";

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool sourcesThere() {
    return fs::exists(fs::path(kRoot) / "com/hulabunny/bunnyPaint/BP_Canvas.as");
}

std::vector<fs::path> paintFiles() {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fs::path(kRoot) / "com/hulabunny/bunnyPaint")) {
        if (entry.is_regular_file() && entry.path().extension() == ".as") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    files.push_back(fs::path(kRoot) / "as3corelib-.93/src/com/adobe/images/JPGEncoder.as");
    files.push_back(fs::path(kRoot) / "as3corelib-.93/src/com/adobe/images/BitString.as");
    return files;
}

// The flash.* and fl.* classes the program imports, as empty classes with
// their superclasses.
void defineStandIns(VM& vm) {
    const std::vector<std::pair<const char*, const char*>> classes = {
        {"flash.events.EventDispatcher", nullptr},
        {"flash.display.DisplayObject", "flash.events.EventDispatcher"},
        {"flash.display.InteractiveObject", "flash.display.DisplayObject"},
        {"flash.display.DisplayObjectContainer", "flash.display.InteractiveObject"},
        {"flash.display.Sprite", "flash.display.DisplayObjectContainer"},
        {"flash.display.MovieClip", "flash.display.Sprite"},
        {"flash.display.Shape", "flash.display.DisplayObject"},
        {"flash.display.Bitmap", "flash.display.DisplayObject"},
        {"flash.display.SimpleButton", "flash.display.InteractiveObject"},
        {"flash.display.Loader", "flash.display.DisplayObjectContainer"},
        {"flash.display.BitmapData", nullptr},
        {"flash.display.BlendMode", nullptr},
        {"flash.display.CapsStyle", nullptr},
        {"flash.display.GradientType", nullptr},
        {"flash.display.LineScaleMode", nullptr},
        {"flash.text.TextField", "flash.display.InteractiveObject"},
        {"flash.events.Event", nullptr},
        {"flash.events.MouseEvent", "flash.events.Event"},
        {"flash.filters.BitmapFilter", nullptr},
        {"flash.filters.BevelFilter", "flash.filters.BitmapFilter"},
        {"flash.filters.BlurFilter", "flash.filters.BitmapFilter"},
        {"flash.filters.ColorMatrixFilter", "flash.filters.BitmapFilter"},
        {"flash.filters.DropShadowFilter", "flash.filters.BitmapFilter"},
        {"flash.filters.GlowFilter", "flash.filters.BitmapFilter"},
        {"flash.filters.BitmapFilterQuality", nullptr},
        {"flash.geom.ColorTransform", nullptr},
        {"flash.geom.Matrix", nullptr},
        {"flash.geom.Point", nullptr},
        {"flash.geom.Rectangle", nullptr},
        {"flash.net.URLRequest", nullptr},
        {"flash.utils.ByteArray", nullptr},
        {"fl.core.UIComponent", "flash.display.Sprite"},
        {"fl.controls.Button", "fl.core.UIComponent"},
        {"fl.controls.CheckBox", "fl.core.UIComponent"},
        {"fl.controls.ColorPicker", "fl.core.UIComponent"},
        {"fl.controls.ComboBox", "fl.core.UIComponent"},
        {"fl.controls.List", "fl.core.UIComponent"},
        {"fl.controls.NumericStepper", "fl.core.UIComponent"},
        {"fl.controls.Slider", "fl.core.UIComponent"},
        {"fl.controls.SliderDirection", nullptr},
        {"fl.data.DataProvider", "flash.events.EventDispatcher"},
        {"fl.events.ColorPickerEvent", "flash.events.Event"},
        {"fl.events.ListEvent", "flash.events.Event"},
        {"fl.events.SliderEvent", "flash.events.Event"},
        {"fl.motion.AdjustColor", nullptr},
    };
    for (const auto& entry : classes) {
        NativeClassBuilder builder = vm.defineNativeClass(entry.first);
        if (entry.second) builder.extends(entry.second);
        builder.dynamic().define();
    }
}

void setResolver(VM& vm, std::vector<std::string>* loaded) {
    vm.setClassResolver([loaded](const std::string& name) -> std::optional<ClassSource> {
        const std::string paint = "com.hulabunny.bunnyPaint.";
        const std::string images = "com.adobe.images.";
        fs::path path;
        if (name.compare(0, paint.size(), paint) == 0) {
            path = fs::path(kRoot) / "com/hulabunny/bunnyPaint" / (name.substr(paint.size()) + ".as");
        } else if (name.compare(0, images.size(), images) == 0) {
            path = fs::path(kRoot) / "as3corelib-.93/src/com/adobe/images" / (name.substr(images.size()) + ".as");
        } else {
            return std::nullopt;
        }
        if (!fs::exists(path)) {
            return std::nullopt;
        }
        if (loaded) loaded->push_back(name);
        return ClassSource{readFile(path), path.filename().string()};
    });
}

} // namespace

static void init_bunnypaint_tests() {
    auto& suite = suite_BunnyPaint;

    suite.addTest("Every file parses and compiles", []() {
        if (!sourcesThere()) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        std::string problems;
        size_t count = 0;
        for (const fs::path& file : paintFiles()) {
            const std::string name = file.filename().string();
            Parser parser(readFile(file), name);
            auto program = parser.parse();
            ++count;
            if (parser.hasErrors()) {
                problems += parser.getErrors();
                continue;
            }
            // The one known slip: MultiSmudgeTool.as line 50 has
            // "mBlendOptions.bleed 0.07;" with its = left out.
            const std::string& warnings = parser.getWarnings();
            if (!warnings.empty() &&
                !(name == "MultiSmudgeTool.as" && warnings.find("at line 50,") != std::string::npos &&
                  std::count(warnings.begin(), warnings.end(), '\n') == 1)) {
                problems += warnings;
            }
            try {
                Compiler compiler;
                compiler.compile(program.get());
            } catch (const std::exception& failed) {
                problems += name + ": " + failed.what() + "\n";
            }
        }
        if (!problems.empty()) {
            throw std::runtime_error("\n" + problems);
        }
        if (count < 43) {
            throw std::runtime_error("only " + std::to_string(count) + " files");
        }
    });

    suite.addTest("Strict mode rejects the slip", []() {
        if (!sourcesThere()) return;
        Parser parser(readFile(fs::path(kRoot) / "com/hulabunny/bunnyPaint/MultiSmudgeTool.as"), "MultiSmudgeTool.as");
        parser.setStrict(true);
        parser.parse();
        if (!parser.hasErrors() || parser.getErrors().find("at line 50,") == std::string::npos) {
            throw std::runtime_error("no error at line 50: " + parser.getErrors());
        }
    });

    suite.addTest("Every class loads through the class resolver", []() {
        if (!sourcesThere()) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        VM vm;
        defineStandIns(vm);
        std::vector<std::string> loaded;
        setResolver(vm, &loaded);
        std::string problems;
        for (const fs::path& file : paintFiles()) {
            const std::string stem = file.stem().string();
            const std::string name =
                (file.parent_path().filename() == "images" ? "com.adobe.images." : "com.hulabunny.bunnyPaint.") + stem;
            try {
                Val found = vm.findDefinition(name);
                if (!found.isClass()) {
                    problems += name + ": not defined\n";
                }
            } catch (ScriptException& failed) {
                problems += name + ": " + vm.toString(failed.value) + "\n";
            }
        }
        for (const std::string& warning : vm.getWarnings()) {
            if (warning.find("MultiSmudgeTool.as at line 50,") != std::string::npos) continue;
            problems += "warning: " + warning + "\n";
        }
        if (!problems.empty()) {
            throw std::runtime_error("\n" + problems);
        }
        std::cout << "    " << loaded.size() << " classes loaded" << std::endl;
    });

    suite.addTest("Classes that need no display run", []() {
        if (!sourcesThere()) {
            std::cout << "    (sources not found; skipped)" << std::endl;
            return;
        }
        VM vm;
        defineStandIns(vm);
        // A ByteArray of floats, as BP_Random keeps its numbers.
        struct Floats : HostObject {
            std::vector<float> values;
            size_t position = 0;
        };
        vm.defineNativeClass("flash.utils.ByteArray")
            .constructor([](VM&, const Val& self, Args) {
                self->host = std::make_shared<Floats>();
                return Val();
            })
            .method("writeFloat", [](VM&, const Val& self, Args args) {
                Floats* floats = self.host<Floats>();
                floats->values.push_back(static_cast<float>(args.number(0)));
                floats->position = floats->values.size() * 4;
                return Val();
            })
            .method("readFloat", [](VM&, const Val& self, Args) {
                Floats* floats = self.host<Floats>();
                const size_t index = floats->position / 4;
                floats->position += 4;
                return Val::number(index < floats->values.size() ? floats->values[index] : 0.0);
            })
            .getter("length", [](VM&, const Val& self, Args) {
                return Val::number(static_cast<double>(self.host<Floats>()->values.size() * 4));
            })
            .property("position", [](VM&, const Val& self, Args) {
                return Val::number(static_cast<double>(self.host<Floats>()->position));
            }, [](VM&, const Val& self, Args args) {
                self.host<Floats>()->position = args[0].toUint32();
                return Val();
            })
            .define();
        setResolver(vm, nullptr);
        std::string out;
        vm.defineFunction("trace", [&out](VM& vm, const Val&, Args args) {
            for (size_t i = 0; i < args.size(); ++i) out += (i ? " " : "") + vm.toString(args[i]);
            out += "\n";
            return Val();
        });
        vm.load("import com.hulabunny.bunnyPaint.*;\n"
                "var c = BP_FloatColor.fromInt(0x336699);\n"
                "c.mixWith(new BP_FloatColor(1, 1, 1), 0.5);\n"
                "trace(c.uintValue.toString(16), c.uintValue);\n"
                "var random = new BP_Random(); random.reset(5); var v = random.value();\n"
                "var copy = new BP_Random(random); copy.reset(5);\n"
                "trace(v >= 0 && v < 1, copy.value() >= 0);\n"
                "var options = new BP_ToolOptions(15, 0, 0.6);\n"
                "options.brushSize = 7.2; options.brushAlpha = 2;\n"
                "trace(options.brushSize, options.brushSizeInt, options.sizeInt, options.brushAlpha, options.brushAlphaInt, options.type == BP_ToolOptions.B_ROUND_AIRBRUSH, options.spacing);\n"
                "var dab = new BP_BrushDab(); dab.width = 4; dab.setPosition(10, 10);\n"
                "trace(dab.x, dab.y, dab.brush);\n",
                "paint.as");
        if (vm.hasError()) {
            throw std::runtime_error(vm.getError() + "\n" + vm.getErrorStack());
        }
        if (out != "99b2cc 10072780\ntrue true\n7.2 8 16 1 255 true 0.035\n0 0 null\n") {
            throw std::runtime_error("got \"" + out + "\"");
        }
    });
}

static struct BunnyPaintTestInit {
    BunnyPaintTestInit() { init_bunnypaint_tests(); }
} bunnypaint_test_init;
