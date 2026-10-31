#ifndef DY_MLAGENTS_SIDE_CHANNELS_H
#define DY_MLAGENTS_SIDE_CHANNELS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dy::mlagents {

// Side channels (mlagents-bridge.md): a payload is a sequence of [16-byte channel id][int32 length][bytes], little
// endian, the id in .NET Guid.ToByteArray / Python uuid.bytes_le order (SideChannelManager.cs:139-141,
// side_channel_manager.py:22-24, :58-59)
struct ChannelId {
    std::array<uint8_t, 16> bytes {};       // bytes_le
    static ChannelId parse(const char* uuid);
    bool operator==(const ChannelId& o) const { return bytes == o.bytes; }
};

// Channel ids of the fork (environment_parameters_channel.py:25, engine_configuration_channel.py:44,
// stats_side_channel.py:37)
ChannelId envParamsChannel();
ChannelId engineConfigChannel();
ChannelId statsChannel();

struct ChannelMessage {
    ChannelId channel;
    std::string payload;
};

// false on a truncated payload
bool readChannelMessages(const std::string& data, std::vector<ChannelMessage>& out);
std::string writeChannelMessages(const std::vector<ChannelMessage>& messages);

// Message fields, as incoming_message.py / outgoing_message.py: little endian, strings as int32 length + ascii
class MessageReader {
public:
    explicit MessageReader(const std::string& payload) : data(payload) {}
    bool readInt32(int32_t& out);
    bool readFloat32(float& out);
    bool readString(std::string& out);
    bool readFloat32List(std::vector<float>& out);
    bool atEnd() const { return offset >= data.size(); }

private:
    const std::string& data;
    size_t offset = 0;
};

class MessageWriter {
public:
    void writeInt32(int32_t v);
    void writeFloat32(float v);
    void writeString(const std::string& s);
    std::string payload;
};

// Environment parameters channel (environment_parameters_channel.py): FLOAT or SAMPLER
struct EnvParamMessage {
    enum class Type : int32_t { Float = 0, Sampler = 1 };
    enum class Sampler : int32_t { Uniform = 0, Gaussian = 1, MultiRangeUniform = 2 };
    std::string key;
    Type type = Type::Float;
    float value = 0.f;                      // Float
    int32_t seed = 0;                       // Sampler
    Sampler sampler = Sampler::Uniform;
    std::vector<float> args;                // uniform (min, max), gaussian (mean, sd), multirange (flattened intervals)
};
bool readEnvParam(const std::string& payload, EnvParamMessage& out);

// Engine configuration channel (engine_configuration_channel.py): accepted and ignored by the engine
struct EngineConfigMessage {
    enum class Type : int32_t { ScreenResolution = 0, QualityLevel = 1, TimeScale = 2, TargetFrameRate = 3, CaptureFrameRate = 4 };
    Type type = Type::TimeScale;
    std::vector<float> values;              // ints widened to float
};
bool readEngineConfig(const std::string& payload, EngineConfigMessage& out);

// Stats channel, engine to trainer (StatsSideChannel.cs; read by stats_side_channel.py:48-50)
enum class StatAggregation : int32_t { Average = 0, MostRecent = 1, Sum = 2, Histogram = 3 };
std::string writeStat(const std::string& key, float value, StatAggregation aggregation);

}

#endif
