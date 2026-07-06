#include "StateCodec.h"

namespace custos
{
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState,
                                  int identityN, const std::array<std::uint8_t, 16>& route,
                                  bool mainLROnly)
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4);
    os.writeByte (4);
    const char* utf8 = path.toRawUTF8();
    const int pathLen = (int) std::strlen (utf8);
    os.writeInt (pathLen);
    os.write (utf8, (size_t) pathLen);
    os.writeInt ((int) innerState.getSize());
    os.write (innerState.getData(), innerState.getSize());
    os.writeInt (identityN);
    os.write (route.data(), route.size());   // 16 bytes
    os.writeByte (mainLROnly ? 1 : 0);       // v4: +1 byte
    os.flush();
    return mb;
}

bool parseState (const void* data, int size, PersistedState& out)
{
    if (data == nullptr || size < 5) return false;
    juce::MemoryInputStream is (data, (size_t) size, false);

    char magic[4] = {};
    if (is.read (magic, 4) != 4 || std::memcmp (magic, "CUS1", 4) != 0) return false;
    const int version = is.readByte();
    if (version < 1 || version > 4) return false;

    const int pathLen = is.readInt();
    if (pathLen < 0 || (juce::int64) pathLen > is.getNumBytesRemaining()) return false;
    juce::MemoryBlock pathBytes;
    is.readIntoMemoryBlock (pathBytes, pathLen);
    juce::String path = juce::String::fromUTF8 ((const char*) pathBytes.getData(), pathLen);

    const int innerLen = is.readInt();
    if (innerLen < 0 || (juce::int64) innerLen > is.getNumBytesRemaining()) return false;
    juce::MemoryBlock inner;
    is.readIntoMemoryBlock (inner, innerLen);

    int identityN = 0;
    if (version >= 2)
    {
        if (is.getNumBytesRemaining() < 4) return false;
        identityN = is.readInt();
    }

    std::array<std::uint8_t, 16> route { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };
    if (version >= 3)
    {
        if (is.getNumBytesRemaining() < 16) return false;
        is.read (route.data(), 16);
    }

    bool mainLROnly = false;
    if (version >= 4)
    {
        if (is.getNumBytesRemaining() < 1) return false;
        mainLROnly = is.readByte() != 0;
    }

    out.path = path;
    out.innerState = std::move (inner);
    out.identityN = identityN;
    out.route = route;
    out.mainLROnly = mainLROnly;
    return true;
}
}
