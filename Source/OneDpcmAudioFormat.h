#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

class OneDpcmAudioFormatReader : public juce::AudioFormatReader
{
public:
    OneDpcmAudioFormatReader(juce::InputStream* sourceStream);
    ~OneDpcmAudioFormatReader() override = default;

    bool readSamples(int* const* destChannels,
                     int numDestChannels,
                     int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile,
                     int numSamples) override;

private:
    juce::MemoryBlock fileData;
    std::vector<int16_t> decodedSamples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OneDpcmAudioFormatReader)
};

class OneDpcmAudioFormat : public juce::AudioFormat
{
public:
    OneDpcmAudioFormat();
    ~OneDpcmAudioFormat() override = default;

    juce::Array<int> getPossibleSampleRates() override;
    juce::Array<int> getPossibleBitDepths() override;
    bool canDoStereo() override;
    bool canDoMono() override;

    std::unique_ptr<juce::AudioFormatWriter> createWriterFor(
        std::unique_ptr<juce::OutputStream>& streamToWriteTo,
        const juce::AudioFormatWriterOptions& options) override;

    bool canHandleFile(const juce::File& fileToTest) override;

    juce::AudioFormatReader* createReaderFor(
        juce::InputStream* sourceStream,
        bool deleteStreamIfOpeningFails) override;
};