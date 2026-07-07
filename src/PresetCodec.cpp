#include "PresetCodec.h"

namespace custos
{
static void writeStr (juce::MemoryOutputStream& os, const juce::String& s)
{
    const char* utf8 = s.toRawUTF8();
    const int len = (int) std::strlen (utf8);
    os.writeInt (len);
    os.write (utf8, (size_t) len);
}

static bool readStr (juce::MemoryInputStream& is, juce::String& out)
{
    if (is.getNumBytesRemaining() < 4) return false;
    const int len = is.readInt();
    if (len < 0 || (juce::int64) len > is.getNumBytesRemaining()) return false;
    juce::MemoryBlock mb;
    is.readIntoMemoryBlock (mb, len);
    out = juce::String::fromUTF8 ((const char*) mb.getData(), len);
    return true;
}

juce::MemoryBlock serializePreset (const PresetData& p)
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUSP", 4);
    os.writeByte (1);
    writeStr (os, p.classId);
    writeStr (os, p.synthName);
    writeStr (os, p.presetName);
    os.writeInt ((int) p.innerState.getSize());
    os.write (p.innerState.getData(), p.innerState.getSize());
    os.flush();
    return mb;
}

bool parsePreset (const void* data, int size, PresetData& out)
{
    if (data == nullptr || size < 5) return false;
    juce::MemoryInputStream is (data, (size_t) size, false);
    char magic[4] = {};
    if (is.read (magic, 4) != 4 || std::memcmp (magic, "CUSP", 4) != 0) return false;
    const int version = is.readByte();
    if (version != 1) return false;

    PresetData p;
    if (! readStr (is, p.classId))   return false;
    if (! readStr (is, p.synthName)) return false;
    if (! readStr (is, p.presetName))return false;
    if (is.getNumBytesRemaining() < 4) return false;
    const int innerLen = is.readInt();
    if (innerLen < 0 || (juce::int64) innerLen > is.getNumBytesRemaining()) return false;
    is.readIntoMemoryBlock (p.innerState, innerLen);

    out = std::move (p);
    return true;
}
}
