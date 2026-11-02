#include "OneDpcmAudioFormat.h"
#include <cmath>

namespace {
    constexpr double SAMPLE_RATE = 192000.0;
    constexpr int NUM_CHANNELS = 2;
    constexpr float INITIAL_STEP = 0.005f;
    constexpr float GROWTH = 1.1f;
    constexpr float DECAY = 0.9f;
    constexpr float MIN_STEP = 0.0001f;
    constexpr float MAX_STEP = 0.5f;
}

OneDpcmAudioFormatReader::OneDpcmAudioFormatReader(juce::InputStream* sourceStream)
    : juce::AudioFormatReader(sourceStream, "1dpcm")
{
    sourceStream->setPosition(0);
    sourceStream->readIntoMemoryBlock(fileData);

    const auto* rawData = static_cast<const uint8_t*>(fileData.getData());
    const size_t dataSize = fileData.getSize();

    const juce::String expectedHeader = "1dpcm-0ver-192khz-2chan";
    const size_t headerLen = static_cast<size_t>(expectedHeader.length());

    if (dataSize < headerLen || memcmp(rawData, expectedHeader.toRawUTF8(), headerLen) != 0)
    {
        lengthInSamples = 0;
        return;
    }

    size_t dataStart = headerLen;
    while (dataStart < dataSize && rawData[dataStart] != '\n')
        dataStart++;

    dataStart++; // Skip newline

    if (dataStart >= dataSize)
    {
        lengthInSamples = 0;
        return;
    }

    const size_t bitstreamSize = dataSize - dataStart;
    const size_t totalBits = bitstreamSize * 8;
    const size_t totalFrames = totalBits / 2;

    // decodedSamples needs exactly totalBits size (since 2 channels * totalFrames = totalBits)
    decodedSamples.resize(totalBits, 0); 
    
    lengthInSamples = static_cast<juce::int64>(totalFrames);
    sampleRate = SAMPLE_RATE;
    numChannels = NUM_CHANNELS;
    usesFloatingPointData = false; // We provide 32-bit integers

    float pred[2] = { 0.0f, 0.0f };
    float step[2] = { INITIAL_STEP, INITIAL_STEP };
    int prevBit[2] = { -1, -1 };

    size_t frameIndex = 0;

    for (size_t byteIdx = 0; byteIdx < bitstreamSize; ++byteIdx)
    {
        const uint8_t byte = rawData[dataStart + byteIdx];

        for (int shift = 7; shift >= 0; --shift)
        {
            if (frameIndex >= totalBits)
                break;

            const int bit = (byte >> shift) & 1;
            const int ch = static_cast<int>(frameIndex % 2);

            pred[ch] += (bit ? step[ch] : -step[ch]);
            const float clamped = juce::jlimit(-1.0f, 1.0f, pred[ch]);
            
            decodedSamples[frameIndex] = static_cast<int16_t>(clamped * 32767.0f);

            if (prevBit[ch] != -1)
            {
                if (bit == prevBit[ch])
                    step[ch] = juce::jmin(step[ch] * GROWTH, MAX_STEP);
                else
                    step[ch] = juce::jmax(step[ch] * DECAY, MIN_STEP);
            }

            prevBit[ch] = bit;
            frameIndex++;
        }
    }
}

bool OneDpcmAudioFormatReader::readSamples(int* const* destChannels,
                                           int numDestChannels,
                                           int startOffsetInDestBuffer,
                                           juce::int64 startSampleInFile,
                                           int numSamples)
{
    if (numSamples <= 0)
        return true;

    // If the DAW asks for audio past the end of our file, fill the buffer with 
    // pure silence and return TRUE. Returning FALSE here crashes some DAWs.
    if (startSampleInFile >= lengthInSamples)
    {
        for (int ch = 0; ch < numDestChannels; ++ch)
        {
            juce::zeromem(destChannels[ch] + startOffsetInDestBuffer, 
                          sizeof(int) * static_cast<size_t>(numSamples));
        }
        return true;
    }

    const juce::int64 available =
        juce::jmin(static_cast<juce::int64>(numSamples), lengthInSamples - startSampleInFile);

    const int samplesToClear = numSamples - static_cast<int>(available);

    for (int ch = 0; ch < numDestChannels; ++ch)
    {
        // 1. Copy the actual audio data we decoded
        for (juce::int64 i = 0; i < available; ++i)
        {
            const size_t sampleIndex = static_cast<size_t>(
                (startSampleInFile + i) * NUM_CHANNELS + (ch % NUM_CHANNELS));

            destChannels[ch][startOffsetInDestBuffer + i] =
                static_cast<int>(decodedSamples[sampleIndex]) << 16;
        }

        // 2. If we hit the end of the file mid-block, fill the rest with silence
        if (samplesToClear > 0)
        {
            juce::zeromem(destChannels[ch] + startOffsetInDestBuffer + available, 
                          sizeof(int) * static_cast<size_t>(samplesToClear));
        }
    }

    return true;
}

OneDpcmAudioFormat::OneDpcmAudioFormat()
    : AudioFormat(juce::String("1dpcm"), juce::StringArray({ ".1dpcm" }))
{
}

juce::Array<int> OneDpcmAudioFormat::getPossibleSampleRates()
{
    return { 192000 };
}

juce::Array<int> OneDpcmAudioFormat::getPossibleBitDepths()
{
    return { 16 };
}

bool OneDpcmAudioFormat::canDoStereo()
{
    return true;
}

bool OneDpcmAudioFormat::canDoMono()
{
    return false;
}

std::unique_ptr<juce::AudioFormatWriter> OneDpcmAudioFormat::createWriterFor(
    std::unique_ptr<juce::OutputStream>& streamToWriteTo,
    const juce::AudioFormatWriterOptions& options)
{
    juce::ignoreUnused(streamToWriteTo, options);
    return nullptr;
}

bool OneDpcmAudioFormat::canHandleFile(const juce::File& fileToTest)
{
    return fileToTest.hasFileExtension("1dpcm");
}

juce::AudioFormatReader* OneDpcmAudioFormat::createReaderFor(
    juce::InputStream* sourceStream,
    bool deleteStreamIfOpeningFails)
{
    auto* reader = new OneDpcmAudioFormatReader(sourceStream);

    if (reader->lengthInSamples > 0)
        return reader;

    delete reader;

    if (deleteStreamIfOpeningFails)
        delete sourceStream;

    return nullptr;
}