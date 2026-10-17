#pragma once

// Enough of SWF for the tests: inflating a compressed (CWS) movie and
// finding its tags, to take the ABC from a real published movie.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace as3test {

// zlib (RFC 1950 and 1951) inflate.
class Inflater {
public:
    static std::vector<uint8_t> inflateZlib(const uint8_t* data, size_t size) {
        if (size < 2) throw std::runtime_error("zlib: too short");
        return inflateRaw(data + 2, size - 2);
    }
    // Deflate data without zlib's header, as a zip entry has it.
    static std::vector<uint8_t> inflateRaw(const uint8_t* data, size_t size) {
        Inflater in(data, size);
        in.run();
        return std::move(in.out);
    }

private:
    const uint8_t* data;
    size_t size;
    size_t at = 0;
    uint32_t bits = 0;
    int count = 0;
    std::vector<uint8_t> out;

    struct Huffman {
        uint16_t counts[16] = {};
        std::vector<uint16_t> symbols;
    };

    Inflater(const uint8_t* d, size_t s) : data(d), size(s) {}

    int bit() {
        if (count == 0) {
            if (at >= size) throw std::runtime_error("zlib: data ends early");
            bits = data[at++];
            count = 8;
        }
        const int value = bits & 1;
        bits >>= 1;
        --count;
        return value;
    }
    int read(int n) {
        int value = 0;
        for (int i = 0; i < n; ++i) value |= bit() << i;
        return value;
    }
    static Huffman build(const uint8_t* lengths, int n) {
        Huffman h;
        for (int i = 0; i < n; ++i) h.counts[lengths[i]]++;
        h.counts[0] = 0;
        uint16_t offsets[16] = {};
        for (int i = 1; i < 16; ++i) offsets[i] = static_cast<uint16_t>(offsets[i - 1] + h.counts[i - 1]);
        h.symbols.resize(n);
        for (int i = 0; i < n; ++i) {
            if (lengths[i]) h.symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
        return h;
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bit();
            const int n = h.counts[len];
            if (code - n < first) return h.symbols[index + (code - first)];
            index += n;
            first += n;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("zlib: bad code");
    }
    void codes(const Huffman& lengths, const Huffman& distances) {
        static const uint16_t lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                         35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t lextra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t dextra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lengths);
            if (symbol < 256) {
                out.push_back(static_cast<uint8_t>(symbol));
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) throw std::runtime_error("zlib: bad length");
                const int length = lbase[symbol] + read(lextra[symbol]);
                const int d = decode(distances);
                if (d >= 30) throw std::runtime_error("zlib: bad distance");
                const size_t distance = dbase[d] + read(dextra[d]);
                if (distance > out.size()) throw std::runtime_error("zlib: distance too far");
                for (int i = 0; i < length; ++i) out.push_back(out[out.size() - distance]);
            }
        }
    }
    void run() {
        int last;
        do {
            last = bit();
            const int type = read(2);
            if (type == 0) {
                count = 0;
                if (at + 4 > size) throw std::runtime_error("zlib: data ends early");
                const size_t len = data[at] | (data[at + 1] << 8);
                at += 4;
                if (at + len > size) throw std::runtime_error("zlib: data ends early");
                out.insert(out.end(), data + at, data + at + len);
                at += len;
            } else if (type == 1) {
                uint8_t lengths[288 + 30];
                int i = 0;
                for (; i < 144; ++i) lengths[i] = 8;
                for (; i < 256; ++i) lengths[i] = 9;
                for (; i < 280; ++i) lengths[i] = 7;
                for (; i < 288; ++i) lengths[i] = 8;
                for (i = 0; i < 30; ++i) lengths[288 + i] = 5;
                codes(build(lengths, 288), build(lengths + 288, 30));
            } else if (type == 2) {
                const int nlen = read(5) + 257, ndist = read(5) + 1, ncode = read(4) + 4;
                static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                uint8_t lengths[320] = {};
                for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<uint8_t>(read(3));
                const Huffman codeLengths = build(lengths, 19);
                int index = 0;
                uint8_t all[320] = {};
                while (index < nlen + ndist) {
                    int symbol = decode(codeLengths);
                    if (symbol < 16) {
                        all[index++] = static_cast<uint8_t>(symbol);
                    } else {
                        int repeat = 0;
                        uint8_t value = 0;
                        if (symbol == 16) {
                            if (index == 0) throw std::runtime_error("zlib: bad repeat");
                            value = all[index - 1];
                            repeat = 3 + read(2);
                        } else if (symbol == 17) {
                            repeat = 3 + read(3);
                        } else {
                            repeat = 11 + read(7);
                        }
                        if (index + repeat > nlen + ndist) throw std::runtime_error("zlib: too many lengths");
                        while (repeat--) all[index++] = value;
                    }
                }
                codes(build(all, nlen), build(all + nlen, ndist));
            } else {
                throw std::runtime_error("zlib: bad block type");
            }
        } while (!last);
    }
};

struct SwfTag {
    int code = 0;
    std::vector<uint8_t> data;
};

// A movie's tags; a CWS one inflated first.
inline std::vector<SwfTag> swfTags(const std::vector<uint8_t>& file) {
    if (file.size() < 8) throw std::runtime_error("not a SWF");
    std::vector<uint8_t> body;
    if (file[0] == 'C' && file[1] == 'W' && file[2] == 'S') {
        body = Inflater::inflateZlib(file.data() + 8, file.size() - 8);
    } else if (file[0] == 'F' && file[1] == 'W' && file[2] == 'S') {
        body.assign(file.begin() + 8, file.end());
    } else {
        throw std::runtime_error("not a SWF");
    }
    const int nbits = body.empty() ? 0 : body[0] >> 3;
    size_t at = (5 + nbits * 4 + 7) / 8 + 4;
    std::vector<SwfTag> tags;
    while (at + 2 <= body.size()) {
        const int header = body[at] | (body[at + 1] << 8);
        at += 2;
        SwfTag tag;
        tag.code = header >> 6;
        size_t length = header & 0x3F;
        if (length == 0x3F) {
            if (at + 4 > body.size()) break;
            length = body[at] | (body[at + 1] << 8) | (body[at + 2] << 16) | (static_cast<size_t>(body[at + 3]) << 24);
            at += 4;
        }
        if (at + length > body.size()) throw std::runtime_error("SWF: a tag runs past the end");
        tag.data.assign(body.begin() + static_cast<std::ptrdiff_t>(at), body.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        if (tag.code == 0) break;
        tags.push_back(std::move(tag));
    }
    return tags;
}

// A zip's entry (a SWC's library.swf), stored or deflated; empty when it
// has none of that name.
inline std::vector<uint8_t> zipEntry(const std::vector<uint8_t>& zip, const std::string& name) {
    const auto u16 = [&zip](size_t at) { return static_cast<size_t>(zip[at] | (zip[at + 1] << 8)); };
    const auto u32 = [&u16](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    // The central directory, from its end record, has each entry's sizes.
    size_t end = zip.size() >= 22 ? zip.size() - 22 : 0;
    while (end > 0 && u32(end) != 0x06054b50) --end;
    if (zip.size() < 22 || u32(end) != 0x06054b50) throw std::runtime_error("not a zip");
    size_t at = u32(end + 16);
    for (size_t count = u16(end + 10); count-- > 0 && at + 46 <= zip.size() && u32(at) == 0x02014b50;) {
        const size_t method = u16(at + 10);
        const size_t compressed = u32(at + 20);
        const size_t nameLength = u16(at + 28);
        const std::string entry(zip.begin() + static_cast<std::ptrdiff_t>(at + 46),
                                zip.begin() + static_cast<std::ptrdiff_t>(at + 46 + nameLength));
        const size_t local = u32(at + 42);
        at += 46 + nameLength + u16(at + 30) + u16(at + 32);
        if (entry != name) continue;
        const size_t data = local + 30 + u16(local + 26) + u16(local + 28);
        if (data + compressed > zip.size()) throw std::runtime_error("zip: an entry runs past the end");
        if (method == 0) return std::vector<uint8_t>(zip.begin() + static_cast<std::ptrdiff_t>(data),
                                                     zip.begin() + static_cast<std::ptrdiff_t>(data + compressed));
        if (method == 8) return Inflater::inflateRaw(zip.data() + data, compressed);
        throw std::runtime_error("zip: method " + std::to_string(method));
    }
    return std::vector<uint8_t>();
}

// A DoABC tag's ABC: after its flags and its name.
inline std::vector<uint8_t> doAbcBytes(const SwfTag& tag) {
    size_t at = 4;
    while (at < tag.data.size() && tag.data[at]) ++at;
    return std::vector<uint8_t>(tag.data.begin() + static_cast<std::ptrdiff_t>(at + 1), tag.data.end());
}

} // namespace as3test
