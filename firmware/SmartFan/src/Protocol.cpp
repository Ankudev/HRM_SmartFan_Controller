#include "Protocol.h"

#include <cstring>

namespace fan::wire {

Frame encodeFrame(uint8_t op, uint32_t id, const uint8_t* payload, size_t size) {
    Frame frame{};
    if (size > maxPayload) return frame;
    auto* p = frame.bytes.data();
    p[0] = 'S';
    p[1] = 'F';
    p[2] = version;
    p[3] = op;
    write32(p + 4, id);
    write16(p + 8, uint16_t(size));
    if (size) std::memcpy(p + headerBytes, payload, size);
    write32(p + headerBytes + size, crc32(p, headerBytes + size));
    frame.size = headerBytes + size + trailerBytes;
    return frame;
}

void Stream::expire(uint32_t now) {
    if (frame_.size && elapsed(now, last_) >= fragmentMs) reset();
}

bool Stream::push(uint8_t b, uint32_t now) {
    expire(now);
    last_ = now;
    if ((frame_.size == 0 && b != 'S') || (frame_.size == 1 && b != 'F')) {
        reset();
        if (b != 'S') return false;
    }
    frame_.bytes[frame_.size++] = b;
    if (frame_.size < headerBytes) return false;
    const size_t size = frame_.length();
    if (size > maxPayload) {
        reset();
        return false;
    }
    const size_t body = headerBytes + size;
    if (frame_.size < body + trailerBytes) return false;
    const auto* p = frame_.bytes.data();
    if (read32(p + body) == crc32(p, body)) return true;
    reset();
    return false;
}

Record encodeRecord(const Config& config) {
    Record record{};
    const auto bytes = encode(config);
    record[0] = version;
    std::memcpy(record.data() + 1, bytes.data(), bytes.size());
    write32(record.data() + 1 + configBytes, crc32(record.data(), 1 + configBytes));
    return record;
}

bool decodeRecord(const Record& r, Config& config) {
    return r[0] == version && read32(r.data() + 1 + configBytes) == crc32(r.data(), 1 + configBytes)
        && decode(r.data() + 1, configBytes, config);
}

void Session::open(uint32_t now) {
    close();
    openedAt_ = now;
    open_ = true;
}

void Session::close() {
    open_ = false;
    staged_ = false;
    previous_.size = 0;
    response_.size = 0;
}

bool Session::allowed(bool trusted, uint32_t now) const {
    return open_ && trusted && elapsed(now, openedAt_) < windowMs;
}

Frame Session::reply(const Frame& request, Result result, const Config* config) const {
    std::array<uint8_t, maxPayload> data{};
    data[0] = uint8_t(result);
    if (config) {
        const auto bytes = encode(*config);
        std::memcpy(data.data() + 1, bytes.data(), bytes.size());
    }
    return encodeFrame(request.op() | 0x80, request.id(), data.data(),
                       config ? 1 + configBytes : 1);
}

Frame Session::handle(const Frame& r, uint32_t now, bool trusted, bool& committed) {
    committed = false;
    if (!allowed(trusted, now)) return {};
    if (!r.id() || (previous_.size && r.id() <= previous_.id())) {
        if (previous_.size == r.size && !std::memcmp(previous_.bytes.data(), r.bytes.data(), r.size)) {
            return response_;
        }
        return reply(r, Result::Sequence);
    }

    previous_ = r;
    Result result = Result::Format;
    const Config* body = nullptr;
    Config active;
    if (r.bytes[2] == version) {
        switch (Op(r.op())) {
        case Op::Get:
            if (r.length()) break;
            result = store_.load(active) ? Result::Ok : Result::Empty;
            if (result == Result::Ok) body = &active;
            break;
        case Op::Set:
            staged_ = r.length() == configBytes && decode(r.payload(), r.length(), candidate_);
            result = staged_ ? Result::Ok : Result::Range;
            break;
        case Op::Close:
            if (!r.length()) {
                staged_ = false;
                result = Result::Ok;
            }
            break;
        case Op::Save:
            if (r.length()) break;
            if (!staged_) result = Result::NoCandidate;
            else if (!store_.save(candidate_)) result = Result::Storage;
            else {
                result = Result::Ok;
                staged_ = false;
                committed = true;
                body = &candidate_;
            }
            break;
        }
    }
    response_ = reply(r, result, body);
    return response_;
}

} // namespace fan::wire
