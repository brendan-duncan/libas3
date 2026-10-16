// Date: a time in milliseconds since 1970 (UTC), read and set in local
// time or UTC.

#include "builtins.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

namespace as3 {

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kMsPerDay = 86400000.0;

struct DateState : HostObject {
    double time = kNaN;
};

const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Days since 1970-01-01 of a civil date (month 1-12).
double daysFromCivil(double year, double month, double day) {
    year -= month <= 2 ? 1 : 0;
    const double era = std::floor(year / 400);
    const double yoe = year - era * 400;
    const double mp = std::fmod(month + 9, 12);
    const double doy = std::floor((153 * mp + 2) / 5) + day - 1;
    const double doe = yoe * 365 + std::floor(yoe / 4) - std::floor(yoe / 100) + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(double days, double& year, double& month, double& day) {
    days += 719468;
    const double era = std::floor(days / 146097);
    const double doe = days - era * 146097;
    const double yoe = std::floor((doe - std::floor(doe / 1460) + std::floor(doe / 36524) - std::floor(doe / 146096)) / 365);
    const double doy = doe - (365 * yoe + std::floor(yoe / 4) - std::floor(yoe / 100));
    const double mp = std::floor((5 * doy + 2) / 153);
    day = doy - std::floor((153 * mp + 2) / 5) + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

// The local time zone's offset from UTC at a UTC time, in milliseconds.
double localOffset(double utc) {
    if (std::isnan(utc)) return 0;
    double seconds = std::floor(utc / 1000);
    // Outside what time_t holds: the offset of a year that is.
    if (seconds < 0) seconds = 0;
    if (seconds > 32503680000.0) seconds = 32503680000.0;
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    const double localDays = daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    const double localSeconds = localDays * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    return (localSeconds - static_cast<double>(t)) * 1000;
}

double toLocal(double utc) {
    return utc + localOffset(utc);
}

double toUtc(double local) {
    const double guess = local - localOffset(local);
    return local - localOffset(guess);
}

double timeClip(double time) {
    if (std::isnan(time) || std::fabs(time) > 8.64e15) return kNaN;
    return std::trunc(time) + 0.0;
}

double makeTime(double hours, double minutes, double seconds, double ms) {
    return hours * 3600000 + minutes * 60000 + seconds * 1000 + ms;
}

double makeDay(double year, double month, double day) {
    if (std::isnan(year) || std::isnan(month) || std::isnan(day) || std::isinf(year) || std::isinf(month) ||
        std::isinf(day)) {
        return kNaN;
    }
    year = std::trunc(year);
    month = std::trunc(month);
    const double y = year + std::floor(month / 12);
    const double m = std::fmod(month, 12);
    const double mm = m < 0 ? m + 12 : m;
    return daysFromCivil(y, mm + 1, 1) + std::trunc(day) - 1;
}

// The parts of a time (local or UTC).
struct Parts {
    double year, month, date, day, hours, minutes, seconds, ms;
};

Parts partsOf(double time) {
    Parts parts{};
    const double days = std::floor(time / kMsPerDay);
    double within = time - days * kMsPerDay;
    double year;
    double month;
    double date;
    civilFromDays(days, year, month, date);
    parts.year = year;
    parts.month = month - 1;
    parts.date = date;
    parts.day = std::fmod(std::fmod(days + 4, 7) + 7, 7);
    parts.hours = std::floor(within / 3600000);
    within -= parts.hours * 3600000;
    parts.minutes = std::floor(within / 60000);
    within -= parts.minutes * 60000;
    parts.seconds = std::floor(within / 1000);
    parts.ms = within - parts.seconds * 1000;
    return parts;
}

double fromParts(const Parts& parts) {
    return makeDay(parts.year, parts.month, parts.date) * kMsPerDay +
           makeTime(parts.hours, parts.minutes, parts.seconds, parts.ms);
}

double nowMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string twoDigits(double value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%02d", static_cast<int>(value));
    return text;
}

std::string zoneText(double offsetMs) {
    const int minutes = static_cast<int>(std::lround(offsetMs / 60000));
    char text[16];
    std::snprintf(text, sizeof(text), "GMT%c%02d%02d", minutes < 0 ? '-' : '+', std::abs(minutes) / 60,
                  std::abs(minutes) % 60);
    return text;
}

// Date.parse: the forms ActionScript reads, as "Mon Oct 16 14:22:05
// GMT-0700 2026", "10/16/2026", "2026/10/16 14:22", "Oct 16, 2026".
double parseDate(const std::string& text) {
    double year = kNaN;
    double month = kNaN;
    double day = kNaN;
    double hours = 0;
    double minutes = 0;
    double seconds = 0;
    bool pm = false;
    bool am = false;
    bool hasZone = false;
    double zoneMinutes = 0;
    std::vector<double> loose;
    size_t i = 0;
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    while (i < text.size()) {
        const char c = text[i];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string word;
            while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) word += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i++])));
            for (int m = 0; m < 12; ++m) {
                std::string name = kMonths[m];
                for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (word.compare(0, 3, name) == 0) month = m;
            }
            if (word == "pm") pm = true;
            if (word == "am") am = true;
            if (word == "gmt" || word == "utc" || word == "z") {
                hasZone = true;
                if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
                    const int sign = text[i] == '-' ? -1 : 1;
                    ++i;
                    std::string digits;
                    while (i < text.size() && isDigit(text[i])) digits += text[i++];
                    if (digits.size() >= 3) {
                        const int value = std::atoi(digits.c_str());
                        zoneMinutes = sign * ((value / 100) * 60 + value % 100);
                    } else if (!digits.empty()) {
                        zoneMinutes = sign * std::atoi(digits.c_str()) * 60;
                    }
                }
            }
            continue;
        }
        if (isDigit(c)) {
            std::string digits;
            while (i < text.size() && isDigit(text[i])) digits += text[i++];
            const double number = std::atof(digits.c_str());
            if (i < text.size() && text[i] == ':') {
                hours = number;
                ++i;
                std::string m;
                while (i < text.size() && isDigit(text[i])) m += text[i++];
                minutes = std::atof(m.c_str());
                if (i < text.size() && text[i] == ':') {
                    ++i;
                    std::string s;
                    while (i < text.size() && isDigit(text[i])) s += text[i++];
                    seconds = std::atof(s.c_str());
                }
                continue;
            }
            if (i < text.size() && text[i] == '/') {
                // a/b/c: month/day/year, or year/month/day when it starts
                // with four digits.
                std::vector<double> fields{number};
                while (i < text.size() && text[i] == '/') {
                    ++i;
                    std::string next;
                    while (i < text.size() && isDigit(text[i])) next += text[i++];
                    fields.push_back(std::atof(next.c_str()));
                }
                if (digits.size() == 4 && fields.size() >= 3) {
                    year = fields[0];
                    month = fields[1] - 1;
                    day = fields[2];
                } else if (fields.size() >= 3) {
                    month = fields[0] - 1;
                    day = fields[1];
                    year = fields[2];
                }
                continue;
            }
            loose.push_back(number);
            continue;
        }
        ++i;
    }
    for (double number : loose) {
        if (std::isnan(day) && number <= 31 && !std::isnan(month)) {
            day = number;
        } else if (std::isnan(year)) {
            year = number;
        } else if (std::isnan(day)) {
            day = number;
        }
    }
    if (std::isnan(year) || std::isnan(month) || std::isnan(day)) {
        return kNaN;
    }
    if (pm && hours < 12) hours += 12;
    if (am && hours == 12) hours = 0;
    const double local = makeDay(year, month, day) * kMsPerDay + makeTime(hours, minutes, seconds, 0);
    if (hasZone) {
        return timeClip(local - zoneMinutes * 60000);
    }
    return timeClip(toUtc(local));
}

DateState& dateOf(VM& vm, const Val& self) {
    return hostOf<DateState>(vm, self, "Date");
}

// Time from the arguments of the constructor's (year, month...) form.
double timeFromFields(VM& vm, Args args) {
    double fields[7] = {kNaN, 0, 1, 0, 0, 0, 0};
    for (size_t i = 0; i < 7 && i < args.size(); ++i) {
        fields[i] = vm.toNumber(args[i]);
    }
    double year = fields[0];
    if (!std::isnan(year)) {
        const double whole = std::trunc(year);
        if (whole >= 0 && whole <= 99) year = 1900 + whole;
    }
    return makeDay(year, fields[1], fields[2]) * kMsPerDay + makeTime(fields[3], fields[4], fields[5], fields[6]);
}

std::string dateString(double time, int form) {
    if (std::isnan(time)) return "Invalid Date";
    const double offset = localOffset(time);
    const Parts local = partsOf(time + offset);
    const std::string day = kDays[static_cast<int>(local.day)];
    const std::string month = kMonths[static_cast<int>(local.month)];
    const std::string date = std::to_string(static_cast<int>(local.date));
    const std::string year = std::to_string(static_cast<long long>(local.year));
    const std::string clock = twoDigits(local.hours) + ":" + twoDigits(local.minutes) + ":" + twoDigits(local.seconds);
    const double hour12 = std::fmod(local.hours, 12) == 0 ? 12 : std::fmod(local.hours, 12);
    const std::string clock12 = twoDigits(hour12) + ":" + twoDigits(local.minutes) + ":" + twoDigits(local.seconds) +
                                (local.hours < 12 ? " AM" : " PM");
    switch (form) {
        case 0: return day + " " + month + " " + date + " " + clock + " " + zoneText(offset) + " " + year;
        case 1: return day + " " + month + " " + date + " " + year;
        case 2: return clock + " " + zoneText(offset);
        case 3: return day + " " + month + " " + date + " " + year + " " + clock12;
        case 4: return clock12;
        default: {
            const Parts utc = partsOf(time);
            return std::string(kDays[static_cast<int>(utc.day)]) + " " + kMonths[static_cast<int>(utc.month)] + " " +
                   std::to_string(static_cast<int>(utc.date)) + " " + twoDigits(utc.hours) + ":" +
                   twoDigits(utc.minutes) + ":" + twoDigits(utc.seconds) + " " +
                   std::to_string(static_cast<long long>(utc.year)) + " UTC";
        }
    }
}

enum Field { kYear, kMonth, kDate, kHours, kMinutes, kSeconds, kMs };

// setFullYear(y, m, d), setHours(h, m, s, ms)...: the fields from first on,
// in local time or UTC.
double setFields(VM& vm, double time, Field first, Args args, bool utc) {
    if (std::isnan(time)) {
        if (first != kYear) return kNaN;
        time = 0;
    }
    Parts parts = partsOf(utc ? time : toLocal(time));
    double* fields[] = {&parts.year, &parts.month, &parts.date, &parts.hours, &parts.minutes, &parts.seconds, &parts.ms};
    const int limits[] = {3, 2, 1, 4, 3, 2, 1};
    for (int i = 0; i < limits[first] && static_cast<size_t>(i) < args.size(); ++i) {
        *fields[first + i] = vm.toNumber(args[static_cast<size_t>(i)]);
    }
    if (args.empty()) return kNaN;
    const double made = fromParts(parts);
    return timeClip(utc ? made : toUtc(made));
}

} // namespace

void defineDateBuiltins(VM& vm) {
    NativeClassBuilder date = vm.defineNativeClass("Date");
    date.dynamic()
        .constructor([](VM& vm, const Val& self, Args args) {
            auto state = std::make_shared<DateState>();
            if (args.empty()) {
                state->time = nowMs();
            } else if (args.size() == 1) {
                if (const DateState* other = args[0].host<DateState>()) {
                    state->time = other->time;
                } else {
                    const Val primitive = vm.toPrimitive(args[0], false);
                    if (primitive.isString()) {
                        state->time = parseDate(primitive.str());
                    } else {
                        state->time = timeClip(primitive.toNumber());
                    }
                }
            } else {
                state->time = timeClip(toUtc(timeFromFields(vm, args)));
            }
            self->host = state;
            return Val();
        })
        .callHandler([](VM&, const Val&, Args) { return Val::string(dateString(nowMs(), 0)); })
        .staticMethod("UTC",
                      [](VM& vm, const Val&, Args args) { return Val::number(timeClip(timeFromFields(vm, args))); }, 7)
        .staticMethod("parse",
                      [](VM& vm, const Val&, Args args) { return Val::number(parseDate(vm.toString(args[0]))); }, 1)
        .method("getTime", [](VM& vm, const Val& self, Args) { return Val::number(dateOf(vm, self).time); })
        .method("valueOf", [](VM& vm, const Val& self, Args) { return Val::number(dateOf(vm, self).time); })
        .method("setTime",
                [](VM& vm, const Val& self, Args args) {
                    DateState& state = dateOf(vm, self);
                    state.time = timeClip(vm.toNumber(args[0]));
                    return Val::number(state.time);
                },
                1)
        .method("getTimezoneOffset",
                [](VM& vm, const Val& self, Args) {
                    const double time = dateOf(vm, self).time;
                    if (std::isnan(time)) return Val::number(kNaN);
                    return Val::number(-localOffset(time) / 60000);
                })
        .getter("timezoneOffset",
                [](VM& vm, const Val& self, Args) {
                    const double time = dateOf(vm, self).time;
                    if (std::isnan(time)) return Val::number(kNaN);
                    return Val::number(-localOffset(time) / 60000);
                })
        .property("time", [](VM& vm, const Val& self, Args) { return Val::number(dateOf(vm, self).time); },
                  [](VM& vm, const Val& self, Args args) {
                      dateOf(vm, self).time = timeClip(vm.toNumber(args[0]));
                      return Val();
                  })
        .method("toString", [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 0)); })
        .method("toDateString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 1)); })
        .method("toTimeString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 2)); })
        .method("toLocaleString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 3)); })
        .method("toLocaleDateString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 1)); })
        .method("toLocaleTimeString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 4)); })
        .method("toUTCString",
                [](VM& vm, const Val& self, Args) { return Val::string(dateString(dateOf(vm, self).time, 5)); });

    // getX / setX / x, local and UTC.
    struct FieldInfo {
        const char* name;
        Field field;
        bool settable;
    };
    const FieldInfo fields[] = {{"FullYear", kYear, true},      {"Month", kMonth, true},     {"Date", kDate, true},
                                {"Day", kDate, false},          {"Hours", kHours, true},     {"Minutes", kMinutes, true},
                                {"Seconds", kSeconds, true},    {"Milliseconds", kMs, true}};
    for (const FieldInfo& info : fields) {
        for (int utc = 0; utc < 2; ++utc) {
            const std::string name = info.name;
            const bool isDay = name == "Day";
            const Field field = info.field;
            const bool inUtc = utc != 0;
            NativeMethod getter = [field, inUtc, isDay](VM& vm, const Val& self, Args) {
                const double time = dateOf(vm, self).time;
                if (std::isnan(time)) return Val::number(kNaN);
                const Parts parts = partsOf(inUtc ? time : toLocal(time));
                if (isDay) return Val::number(parts.day);
                const double values[] = {parts.year, parts.month, parts.date, parts.hours,
                                         parts.minutes, parts.seconds, parts.ms};
                return Val::number(values[field]);
            };
            const std::string suffix = inUtc ? "UTC" : "";
            date.method("get" + suffix + name, getter);
            // The property: fullYear, monthUTC...
            std::string property = name;
            property[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(property[0])));
            if (property == "fullYear" || true) {
                date.getter(property + suffix, getter);
            }
            if (info.settable) {
                NativeMethod setter = [field, inUtc](VM& vm, const Val& self, Args args) {
                    DateState& state = dateOf(vm, self);
                    state.time = setFields(vm, state.time, field, args, inUtc);
                    return Val::number(state.time);
                };
                date.method("set" + suffix + name, setter, 1);
                date.setter(property + suffix, [field, inUtc](VM& vm, const Val& self, Args args) {
                    DateState& state = dateOf(vm, self);
                    const Val value = args[0];
                    state.time = setFields(vm, state.time, field, Args(&value, 1), inUtc);
                    return Val();
                });
            }
        }
    }
    vm.builtins().date = date.define()->cls;
}

} // namespace as3
