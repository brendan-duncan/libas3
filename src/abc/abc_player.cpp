#include "abc_player.h"

#include <cstring>
#include <map>
#include <set>

namespace as3 {
namespace abc {

namespace {

const PlayerClass kClasses[] = {
    // The top level.
    {"", "Object", nullptr},
    {"", "Class", "Object"},
    {"", "Function", "Object"},
    {"", "Namespace", "Object"},
    {"", "QName", "Object"},
    {"", "Boolean", "Object"},
    {"", "Number", "Object"},
    {"", "int", "Object"},
    {"", "uint", "Object"},
    {"", "String", "Object"},
    {"", "Array", "Object"},
    {"", "Date", "Object"},
    {"", "RegExp", "Object"},
    {"", "XML", "Object"},
    {"", "XMLList", "Object"},
    {"", "Math", "Object"},
    {"", "JSON", "Object"},
    {"", "Error", "Object"},
    {"", "ArgumentError", "Error"},
    {"", "DefinitionError", "Error"},
    {"", "EvalError", "Error"},
    {"", "RangeError", "Error"},
    {"", "ReferenceError", "Error"},
    {"", "SecurityError", "Error"},
    {"", "SyntaxError", "Error"},
    {"", "TypeError", "Error"},
    {"", "URIError", "Error"},
    {"", "VerifyError", "Error"},
    {"", "UninitializedError", "Error"},
    {"__AS3__.vec", "Vector", "Object"},

    {"flash.accessibility", "Accessibility", "Object"},
    {"flash.accessibility", "AccessibilityProperties", "Object"},

    {"flash.display", "ActionScriptVersion", "Object"},
    {"flash.display", "AVM1Movie", "flash.display.DisplayObject"},
    {"flash.display", "Bitmap", "flash.display.DisplayObject"},
    {"flash.display", "BitmapData", "Object"},
    {"flash.display", "BitmapDataChannel", "Object"},
    {"flash.display", "BlendMode", "Object"},
    {"flash.display", "CapsStyle", "Object"},
    {"flash.display", "DisplayObject", "flash.events.EventDispatcher"},
    {"flash.display", "DisplayObjectContainer", "flash.display.InteractiveObject"},
    {"flash.display", "FrameLabel", "Object"},
    {"flash.display", "GradientType", "Object"},
    {"flash.display", "Graphics", "Object"},
    {"flash.display", "GraphicsBitmapFill", "Object"},
    {"flash.display", "GraphicsEndFill", "Object"},
    {"flash.display", "GraphicsGradientFill", "Object"},
    {"flash.display", "GraphicsPath", "Object"},
    {"flash.display", "GraphicsPathCommand", "Object"},
    {"flash.display", "GraphicsPathWinding", "Object"},
    {"flash.display", "GraphicsShaderFill", "Object"},
    {"flash.display", "GraphicsSolidFill", "Object"},
    {"flash.display", "GraphicsStroke", "Object"},
    {"flash.display", "GraphicsTrianglePath", "Object"},
    {"flash.display", "IBitmapDrawable", nullptr},
    {"flash.display", "IGraphicsData", nullptr},
    {"flash.display", "IGraphicsFill", nullptr},
    {"flash.display", "IGraphicsPath", nullptr},
    {"flash.display", "IGraphicsStroke", nullptr},
    {"flash.display", "InteractiveObject", "flash.display.DisplayObject"},
    {"flash.display", "InterpolationMethod", "Object"},
    {"flash.display", "JointStyle", "Object"},
    {"flash.display", "LineScaleMode", "Object"},
    {"flash.display", "Loader", "flash.display.DisplayObjectContainer"},
    {"flash.display", "LoaderInfo", "flash.events.EventDispatcher"},
    {"flash.display", "MorphShape", "flash.display.DisplayObject"},
    {"flash.display", "MovieClip", "flash.display.Sprite"},
    {"flash.display", "PixelSnapping", "Object"},
    {"flash.display", "Scene", "Object"},
    {"flash.display", "Shader", "Object"},
    {"flash.display", "ShaderData", "Object"},
    {"flash.display", "ShaderInput", "Object"},
    {"flash.display", "ShaderJob", "flash.events.EventDispatcher"},
    {"flash.display", "ShaderParameter", "Object"},
    {"flash.display", "Shape", "flash.display.DisplayObject"},
    {"flash.display", "SimpleButton", "flash.display.InteractiveObject"},
    {"flash.display", "SpreadMethod", "Object"},
    {"flash.display", "Sprite", "flash.display.DisplayObjectContainer"},
    {"flash.display", "Stage", "flash.display.DisplayObjectContainer"},
    {"flash.display", "StageAlign", "Object"},
    {"flash.display", "StageDisplayState", "Object"},
    {"flash.display", "StageQuality", "Object"},
    {"flash.display", "StageScaleMode", "Object"},
    {"flash.display", "TriangleCulling", "Object"},

    {"flash.errors", "EOFError", "flash.errors.IOError"},
    {"flash.errors", "IllegalOperationError", "Error"},
    {"flash.errors", "InvalidSWFError", "Error"},
    {"flash.errors", "IOError", "Error"},
    {"flash.errors", "MemoryError", "Error"},
    {"flash.errors", "ScriptTimeoutError", "Error"},
    {"flash.errors", "StackOverflowError", "Error"},

    {"flash.events", "ActivityEvent", "flash.events.Event"},
    {"flash.events", "AsyncErrorEvent", "flash.events.ErrorEvent"},
    {"flash.events", "ContextMenuEvent", "flash.events.Event"},
    {"flash.events", "DataEvent", "flash.events.TextEvent"},
    {"flash.events", "ErrorEvent", "flash.events.TextEvent"},
    {"flash.events", "Event", "Object"},
    {"flash.events", "EventDispatcher", "Object"},
    {"flash.events", "EventPhase", "Object"},
    {"flash.events", "FocusEvent", "flash.events.Event"},
    {"flash.events", "FullScreenEvent", "flash.events.ActivityEvent"},
    {"flash.events", "GestureEvent", "flash.events.Event"},
    {"flash.events", "HTTPStatusEvent", "flash.events.Event"},
    {"flash.events", "IEventDispatcher", nullptr},
    {"flash.events", "IMEEvent", "flash.events.TextEvent"},
    {"flash.events", "IOErrorEvent", "flash.events.ErrorEvent"},
    {"flash.events", "KeyboardEvent", "flash.events.Event"},
    {"flash.events", "MouseEvent", "flash.events.Event"},
    {"flash.events", "NetStatusEvent", "flash.events.Event"},
    {"flash.events", "ProgressEvent", "flash.events.Event"},
    {"flash.events", "SampleDataEvent", "flash.events.Event"},
    {"flash.events", "SecurityErrorEvent", "flash.events.ErrorEvent"},
    {"flash.events", "StatusEvent", "flash.events.Event"},
    {"flash.events", "SyncEvent", "flash.events.Event"},
    {"flash.events", "TextEvent", "flash.events.Event"},
    {"flash.events", "TimerEvent", "flash.events.Event"},
    {"flash.events", "TouchEvent", "flash.events.Event"},
    {"flash.events", "TransformGestureEvent", "flash.events.GestureEvent"},
    {"flash.events", "UncaughtErrorEvent", "flash.events.ErrorEvent"},
    {"flash.events", "UncaughtErrorEvents", "flash.events.EventDispatcher"},

    {"flash.external", "ExternalInterface", "Object"},

    {"flash.filters", "BevelFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "BitmapFilter", "Object"},
    {"flash.filters", "BitmapFilterQuality", "Object"},
    {"flash.filters", "BitmapFilterType", "Object"},
    {"flash.filters", "BlurFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "ColorMatrixFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "ConvolutionFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "DisplacementMapFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "DisplacementMapFilterMode", "Object"},
    {"flash.filters", "DropShadowFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "GlowFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "GradientBevelFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "GradientGlowFilter", "flash.filters.BitmapFilter"},
    {"flash.filters", "ShaderFilter", "flash.filters.BitmapFilter"},

    {"flash.geom", "ColorTransform", "Object"},
    {"flash.geom", "Matrix", "Object"},
    {"flash.geom", "Matrix3D", "Object"},
    {"flash.geom", "Orientation3D", "Object"},
    {"flash.geom", "PerspectiveProjection", "Object"},
    {"flash.geom", "Point", "Object"},
    {"flash.geom", "Rectangle", "Object"},
    {"flash.geom", "Transform", "Object"},
    {"flash.geom", "Utils3D", "Object"},
    {"flash.geom", "Vector3D", "Object"},

    {"flash.media", "Camera", "flash.events.EventDispatcher"},
    {"flash.media", "ID3Info", "Object"},
    {"flash.media", "Microphone", "flash.events.EventDispatcher"},
    {"flash.media", "Sound", "flash.events.EventDispatcher"},
    {"flash.media", "SoundChannel", "flash.events.EventDispatcher"},
    {"flash.media", "SoundLoaderContext", "Object"},
    {"flash.media", "SoundMixer", "Object"},
    {"flash.media", "SoundTransform", "Object"},
    {"flash.media", "Video", "flash.display.DisplayObject"},

    {"flash.net", "FileFilter", "Object"},
    {"flash.net", "FileReference", "flash.events.EventDispatcher"},
    {"flash.net", "FileReferenceList", "flash.events.EventDispatcher"},
    {"flash.net", "LocalConnection", "flash.events.EventDispatcher"},
    {"flash.net", "NetConnection", "flash.events.EventDispatcher"},
    {"flash.net", "NetStream", "flash.events.EventDispatcher"},
    {"flash.net", "ObjectEncoding", "Object"},
    {"flash.net", "Responder", "Object"},
    {"flash.net", "SharedObject", "flash.events.EventDispatcher"},
    {"flash.net", "SharedObjectFlushStatus", "Object"},
    {"flash.net", "Socket", "flash.events.EventDispatcher"},
    {"flash.net", "URLLoader", "flash.events.EventDispatcher"},
    {"flash.net", "URLLoaderDataFormat", "Object"},
    {"flash.net", "URLRequest", "Object"},
    {"flash.net", "URLRequestHeader", "Object"},
    {"flash.net", "URLRequestMethod", "Object"},
    {"flash.net", "URLStream", "flash.events.EventDispatcher"},
    {"flash.net", "URLVariables", "Object"},
    {"flash.net", "XMLSocket", "flash.events.EventDispatcher"},

    {"flash.printing", "PrintJob", "flash.events.EventDispatcher"},
    {"flash.printing", "PrintJobOptions", "Object"},

    {"flash.system", "ApplicationDomain", "Object"},
    {"flash.system", "Capabilities", "Object"},
    {"flash.system", "IME", "flash.events.EventDispatcher"},
    {"flash.system", "LoaderContext", "Object"},
    {"flash.system", "Security", "Object"},
    {"flash.system", "SecurityDomain", "Object"},
    {"flash.system", "System", "Object"},

    {"flash.text", "AntiAliasType", "Object"},
    {"flash.text", "Font", "Object"},
    {"flash.text", "FontStyle", "Object"},
    {"flash.text", "FontType", "Object"},
    {"flash.text", "GridFitType", "Object"},
    {"flash.text", "StaticText", "flash.display.DisplayObject"},
    {"flash.text", "StyleSheet", "flash.events.EventDispatcher"},
    {"flash.text", "TextColorType", "Object"},
    {"flash.text", "TextDisplayMode", "Object"},
    {"flash.text", "TextField", "flash.display.InteractiveObject"},
    {"flash.text", "TextFieldAutoSize", "Object"},
    {"flash.text", "TextFieldType", "Object"},
    {"flash.text", "TextFormat", "Object"},
    {"flash.text", "TextFormatAlign", "Object"},
    {"flash.text", "TextLineMetrics", "Object"},
    {"flash.text", "TextRenderer", "Object"},
    {"flash.text", "TextSnapshot", "Object"},

    {"flash.ui", "ContextMenu", "flash.events.EventDispatcher"},
    {"flash.ui", "ContextMenuBuiltInItems", "Object"},
    {"flash.ui", "ContextMenuItem", "flash.events.EventDispatcher"},
    {"flash.ui", "Keyboard", "Object"},
    {"flash.ui", "KeyLocation", "Object"},
    {"flash.ui", "Mouse", "Object"},
    {"flash.ui", "MouseCursor", "Object"},
    {"flash.ui", "Multitouch", "Object"},

    {"flash.utils", "ByteArray", "Object"},
    {"flash.utils", "Dictionary", "Object"},
    {"flash.utils", "Endian", "Object"},
    {"flash.utils", "IDataInput", nullptr},
    {"flash.utils", "IDataOutput", nullptr},
    {"flash.utils", "IExternalizable", nullptr},
    {"flash.utils", "Proxy", "Object"},
    {"flash.utils", "Timer", "flash.events.EventDispatcher"},

    {"flash.xml", "XMLDocument", "flash.xml.XMLNode"},
    {"flash.xml", "XMLNode", "Object"},
    {"flash.xml", "XMLNodeType", "Object"},
};

const char* kTopLevelNames[] = {
    "trace", "isNaN", "isFinite", "parseInt", "parseFloat", "escape", "unescape", "encodeURI",
    "encodeURIComponent", "decodeURI", "decodeURIComponent", "isXMLName", "NaN", "Infinity", "undefined",
};

struct Index {
    std::map<std::pair<std::string, std::string>, const PlayerClass*> classes;
    std::set<std::string> packages;
    std::set<std::string> topLevel;

    Index() {
        for (const PlayerClass& entry : kClasses) {
            classes[{entry.package, entry.name}] = &entry;
            packages.insert(entry.package);
            if (!*entry.package) topLevel.insert(entry.name);
        }
        for (const char* name : kTopLevelNames) topLevel.insert(name);
    }
};

const Index& index() {
    static const Index instance;
    return instance;
}

} // namespace

const PlayerClass* findPlayerClass(const std::string& package, const std::string& name) {
    const auto& classes = index().classes;
    auto found = classes.find({package, name});
    return found == classes.end() ? nullptr : found->second;
}

bool isTopLevelName(const std::string& name) {
    return index().topLevel.count(name) > 0;
}

bool isPlayerPackage(const std::string& package) {
    return !package.empty() && index().packages.count(package) > 0;
}

} // namespace abc
} // namespace as3
