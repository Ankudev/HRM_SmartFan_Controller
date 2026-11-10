#pragma once

#include "Core.h"

namespace fan::wire {

constexpr uint8_t version = 2;
constexpr uint32_t fragmentMs = 2000;
constexpr uint32_t windowMs = 180000;
constexpr size_t headerBytes = 10;
constexpr size_t trailerBytes = 4;
constexpr size_t maxPayload = 1 + configBytes;
constexpr size_t maxFrame = headerBytes + maxPayload + trailerBytes;

enum class Op : uint8_t { Get = 1, Set = 2, Save = 3, Close = 4 };
enum class Result : uint8_t { Ok, Format, Range, NoCandidate, Storage, Sequence, Empty };

struct Frame {
    std::array<uint8_t, maxFrame> bytes{};
    size_t size = 0;
    uint8_t op() const { return bytes[3]; }
    uint32_t id() const { return read32(bytes.data() + 4); }
    size_t length() const { return read16(bytes.data() + 8); }
    const uint8_t* payload() const { return bytes.data() + headerBytes; }
};

Frame encodeFrame(uint8_t op, uint32_t id, const uint8_t* payload, size_t size);

class Stream {
public:
    bool push(uint8_t value, uint32_t now);
    void reset() { frame_.size = 0; }
    void expire(uint32_t now);
    const Frame& frame() const { return frame_; }

private:
    Frame frame_;
    uint32_t last_ = 0;
};

class Store {
public:
    virtual ~Store() = default;
    virtual bool load(Config& config) = 0;
    virtual bool save(const Config& config) = 0;
};

constexpr size_t recordBytes = 1 + configBytes + trailerBytes;
using Record = std::array<uint8_t, recordBytes>;
Record encodeRecord(const Config& config);
bool decodeRecord(const Record& record, Config& config);

class Session {
public:
    explicit Session(Store& store) : store_(store) {}
    void open(uint32_t now);
    void close();
    bool allowed(bool trusted, uint32_t now) const;
    Frame handle(const Frame& request, uint32_t now, bool trusted, bool& committed);

private:
    Frame reply(const Frame& request, Result result, const Config* config = nullptr) const;

    Store& store_;
    Frame previous_{}, response_{};
    Config candidate_{};
    uint32_t openedAt_ = 0;
    bool open_ = false;
    bool staged_ = false;
};

} // namespace fan::wire
