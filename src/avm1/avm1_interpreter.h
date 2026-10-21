#pragma once

#include "../runtime/value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace as3 {

class VM;

namespace avm1 {

// The action bytecode of a DoAction or DoInitAction tag, or of a button's or
// a movie clip's handler, as a movie has it (ending with ActionEnd). Shared:
// the functions it defines keep it while they live.
using Actions = std::shared_ptr<const std::vector<uint8_t>>;

// What the movie playing the actions answers for: its movie clips (as the
// objects scripts see them), their timelines, and the player around them.
// The clips are values the host made (host objects, usually); the
// interpreter finds their properties and methods through the VM as it does
// any object's, and asks the host only for what actions do to timelines.
class Host {
public:
    virtual ~Host() = default;

    // What trace gives, and an error that stopped a script.
    virtual void trace(const std::string& text) { (void)text; }
    virtual void error(const std::string& message) { (void)message; }

    // Whether a value is one of the movie's clips (or buttons), and the
    // name of its class ("MovieClip", "Button", "TextField"), whose
    // prototype it finds what scripts add there in.
    virtual bool isClip(const Val& value) { (void)value; return false; }
    virtual std::string className(const Val& clip) { (void)clip; return "MovieClip"; }

    // The movie's main timeline as \a clip sees it (_root), the level
    // numbered (_level0), a clip's parent and its child of that instance
    // name: undefined for none.
    virtual Val root(const Val& clip) { (void)clip; return Val(); }
    virtual Val level(int number) { (void)number; return Val(); }
    virtual Val parent(const Val& clip) { (void)clip; return Val(); }
    virtual Val child(const Val& clip, const std::string& name) { (void)clip; (void)name; return Val(); }
    // The path to a clip as scripts write it: "_level0.hat.bow".
    virtual std::string targetPath(const Val& clip) { (void)clip; return std::string(); }
    // The names of a clip's children, for for-in.
    virtual void childNames(const Val& clip, std::vector<std::string>& names) { (void)clip; (void)names; }

    // A timeline goes to a frame (from 0) or a label, playing on or
    // stopped there; gotoLabel is false when it has no such label.
    virtual void gotoFrame(const Val& clip, int frame, bool play) { (void)clip; (void)frame; (void)play; }
    virtual bool gotoLabel(const Val& clip, const std::string& label, bool play) {
        (void)clip; (void)label; (void)play;
        return false;
    }
    virtual void setPlaying(const Val& clip, bool playing) { (void)clip; (void)playing; }
    virtual void nextFrame(const Val& clip) { (void)clip; }
    virtual void prevFrame(const Val& clip) { (void)clip; }
    // How many of a timeline's frames have loaded (WaitForFrame).
    virtual int framesLoaded(const Val& clip) { (void)clip; return 1 << 30; }
    // Runs the actions on a frame of a timeline, as a subroutine (call()):
    // \a frame is a number (from 1) or a label.
    virtual void callFrame(const Val& clip, const Val& frame) { (void)clip; (void)frame; }

    // A property as GetProperty numbers them, by name ("_x", "_alpha"):
    // undefined lets the interpreter read it as a member of the clip, and
    // false from setProperty sets it as a member.
    virtual Val getProperty(const Val& clip, const std::string& name) { (void)clip; (void)name; return Val(); }
    virtual bool setProperty(const Val& clip, const std::string& name, const Val& value) {
        (void)clip; (void)name; (void)value;
        return false;
    }

    // duplicateMovieClip, removeMovieClip, startDrag and stopDrag.
    virtual void cloneSprite(const Val& clip, const std::string& name, int depth) {
        (void)clip; (void)name; (void)depth;
    }
    virtual void removeSprite(const Val& clip) { (void)clip; }
    virtual void startDrag(const Val& clip, bool lockCenter, bool constrain, double left, double top, double right,
                           double bottom) {
        (void)clip; (void)lockCenter; (void)constrain; (void)left; (void)top; (void)right; (void)bottom;
    }
    virtual void endDrag() {}

    // getURL and its kin: \a method is 0 (none), 1 (GET) or 2 (POST);
    // \a loadTarget when the target is a clip to load a movie into, and
    // \a loadVariables for loadVariables. "FSCommand:..." URLs are
    // fscommand().
    virtual void getURL(const std::string& url, const std::string& target, int method, bool loadTarget,
                        bool loadVariables) {
        (void)url; (void)target; (void)method; (void)loadTarget; (void)loadVariables;
    }
    virtual void toggleQuality() {}
    virtual void stopSounds() {}
    // Milliseconds since the movie started (getTimer).
    virtual double time() { return 0; }
    // MMExecute: script for the authoring tool the movie is a panel of,
    // and the value that comes back, as text. Empty where no tool holds
    // the movie.
    virtual std::string mmExecute(const std::string& code) { (void)code; return std::string(); }

    // The clip whose timeline bare calls to the VM's global functions
    // (gotoAndStop(), getProperty("", ...)) act on changed: tellTarget
    // (SetTarget) sent them to \a clip, or its end sent them back.
    virtual void targetChanged(const Val& clip) { (void)clip; }
};

class Machine;

// Runs AVM1 action bytecode, the ActionScript 1 and 2 of SWF movies (SWF 4
// to 8), on the VM: with its objects, its built-in classes and the host's
// native ones, so that a movie's compiled scripts share what scripts the
// VM compiled from source see. Functions the actions define are VM
// functions (scripts and the host call them as any other), and objects
// they make are VM objects, with ActionScript 1's prototypes kept on them.
class Interpreter {
public:
    // \a version is the SWF version the movie was published for: before 7
    // names are found whatever their case, and undefined counts as 0 and
    // reads as "".
    Interpreter(VM& vm, Host& host, int version = 8);
    ~Interpreter();

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    int version() const;
    void setVersion(int version);

    // Runs a DoAction's (or a DoInitAction's) actions on the timeline
    // \a clip: it is their this and their target, where their variables
    // go. False when an error stopped them; the host was told why, with
    // \a where for where it was.
    bool run(const Actions& actions, const Val& clip, const std::string& where = std::string());
    // A function that runs \a actions on the timeline it is called with as
    // 'this': a button's or a movie clip's handler, for the host to call.
    Val handler(const Actions& actions, const std::string& where = std::string());

    // _global.
    Val global();
    // A value a constructor returns for new to give undefined rather than
    // the object made, as a player's own classes do when given too little.
    Val noObject();
    // A constructor's prototype (MovieClip.prototype), made the first time
    // for a native one.
    Val prototypeOf(const Val& constructor);
    // The constructor Object.registerClass gave a library symbol's linkage
    // name, undefined for none; and a clip made one of its class: the
    // class's prototype is its prototype, and the constructor is run on it.
    Val registeredClass(const std::string& linkage);
    void applyClass(const Val& clip, const Val& constructor);

    // Members as ActionScript 1 finds them, along prototypes; and calls
    // from the host (onEnterFrame, a listener's method), an error told to
    // the host rather than thrown.
    Val getMember(const Val& object, const std::string& name);
    // Whether \a object or a prototype of it has \a name, a value or an
    // accessor.
    bool hasMember(const Val& object, const std::string& name);
    void setMember(const Val& object, const std::string& name, const Val& value);
    Val call(const Val& function, const Val& self, const std::vector<Val>& args,
             const std::string& where = std::string());
    Val callMethod(const Val& object, const std::string& name, const std::vector<Val>& args,
                   const std::string& where = std::string());

    // Conversions as the movie's SWF version has them.
    std::string toString(const Val& value);
    double toNumber(const Val& value);
    bool toBoolean(const Val& value);

private:
    std::unique_ptr<Machine> _machine;
};

} // namespace avm1
} // namespace as3
