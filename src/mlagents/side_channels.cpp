#include "side_channels.h"

#include <cstring>

using namespace dy::mlagents;

ChannelId ChannelId::parse(const char* uuid) {
    // Text order is big endian; bytes_le reverses the first three fields (4, 2 and 2 bytes)
    std::array<uint8_t, 16> be {};
    int n = 0;
    for(const char* c = uuid; *c && n < 32; c++) {
        if(*c == '-') continue;
        int v = (*c >= '0' && *c <= '9') ? *c - '0' : (*c | 32) - 'a' + 10;
        be[n / 2] = (uint8_t) ((be[n / 2] << 4) | v);
        n++;
    }
    ChannelId id;
    const int order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
    for(int i = 0; i < 16; i++) id.bytes[i] = be[order[i]];
    return id;
}

ChannelId dy::mlagents::envParamsChannel() { return ChannelId::parse("534c891e-810f-11ea-a9d0-822485860400"); }
ChannelId dy::mlagents::engineConfigChannel() { return ChannelId::parse("e951342c-4f7e-11ea-b238-784f4387d1f7"); }
ChannelId dy::mlagents::statsChannel() { return ChannelId::parse("a1d8f7b7-cec8-50f9-b78b-d3e165a78520"); }

bool dy::mlagents::readChannelMessages(const std::string& data, std::vector<ChannelMessage>& out) {
    size_t offset = 0;
    while(offset < data.size()) {
        if(data.size() - offset < 20) return false;
        ChannelMessage m;
        std::memcpy(m.channel.bytes.data(), data.data() + offset, 16);
        int32_t length;
        std::memcpy(&length, data.data() + offset + 16, 4);
        offset += 20;
        if(length < 0 || data.size() - offset < (size_t) length) return false;
        m.payload = data.substr(offset, length);
        offset += length;
        out.push_back(std::move(m));
    }
    return true;
}

std::string dy::mlagents::writeChannelMessages(const std::vector<ChannelMessage>& messages) {
    std::string out;
    for(auto& m : messages) {
        out.append(reinterpret_cast<const char*>(m.channel.bytes.data()), 16);
        int32_t length = (int32_t) m.payload.size();
        out.append(reinterpret_cast<const char*>(&length), 4);
        out += m.payload;
    }
    return out;
}

bool MessageReader::readInt32(int32_t& out) {
    if(data.size() - offset < 4 || offset > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 4);
    offset += 4;
    return true;
}

bool MessageReader::readFloat32(float& out) {
    if(data.size() - offset < 4 || offset > data.size()) return false;
    std::memcpy(&out, data.data() + offset, 4);
    offset += 4;
    return true;
}

bool MessageReader::readString(std::string& out) {
    int32_t length;
    if(!readInt32(length) || length < 0 || data.size() - offset < (size_t) length) return false;
    out = data.substr(offset, length);
    offset += length;
    return true;
}

bool MessageReader::readFloat32List(std::vector<float>& out) {
    int32_t count;
    if(!readInt32(count) || count < 0) return false;
    out.resize(count);
    for(auto& f : out) {
        if(!readFloat32(f)) return false;
    }
    return true;
}

void MessageWriter::writeInt32(int32_t v) {
    payload.append(reinterpret_cast<const char*>(&v), 4);
}

void MessageWriter::writeFloat32(float v) {
    payload.append(reinterpret_cast<const char*>(&v), 4);
}

void MessageWriter::writeString(const std::string& s) {
    writeInt32((int32_t) s.size());
    payload += s;
}

bool dy::mlagents::readEnvParam(const std::string& payload, EnvParamMessage& out) {
    MessageReader r(payload);
    int32_t type;
    if(!r.readString(out.key) || !r.readInt32(type)) return false;
    out.type = (EnvParamMessage::Type) type;
    if(out.type == EnvParamMessage::Type::Float) return r.readFloat32(out.value);
    if(out.type != EnvParamMessage::Type::Sampler) return false;
    int32_t sampler;
    if(!r.readInt32(out.seed) || !r.readInt32(sampler)) return false;
    out.sampler = (EnvParamMessage::Sampler) sampler;
    out.args.clear();
    switch(out.sampler) {
    case EnvParamMessage::Sampler::Uniform:
    case EnvParamMessage::Sampler::Gaussian: {
        float a, b;
        if(!r.readFloat32(a) || !r.readFloat32(b)) return false;
        out.args = { a, b };
        return true;
    }
    case EnvParamMessage::Sampler::MultiRangeUniform:
        return r.readFloat32List(out.args);
    }
    return false;
}

bool dy::mlagents::readEngineConfig(const std::string& payload, EngineConfigMessage& out) {
    MessageReader r(payload);
    int32_t type;
    if(!r.readInt32(type)) return false;
    out.type = (EngineConfigMessage::Type) type;
    out.values.clear();
    switch(out.type) {
    case EngineConfigMessage::Type::ScreenResolution: {
        int32_t w, h;
        if(!r.readInt32(w) || !r.readInt32(h)) return false;
        out.values = { (float) w, (float) h };
        return true;
    }
    case EngineConfigMessage::Type::TimeScale: {
        float t;
        if(!r.readFloat32(t)) return false;
        out.values = { t };
        return true;
    }
    case EngineConfigMessage::Type::QualityLevel:
    case EngineConfigMessage::Type::TargetFrameRate:
    case EngineConfigMessage::Type::CaptureFrameRate: {
        int32_t v;
        if(!r.readInt32(v)) return false;
        out.values = { (float) v };
        return true;
    }
    }
    return false;
}

std::string dy::mlagents::writeStat(const std::string& key, float value, StatAggregation aggregation) {
    MessageWriter w;
    w.writeString(key);
    w.writeFloat32(value);
    w.writeInt32((int32_t) aggregation);
    return w.payload;
}
