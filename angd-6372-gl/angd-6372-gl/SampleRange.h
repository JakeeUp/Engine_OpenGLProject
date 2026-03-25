#pragma once

template<typename TSampleType, size_t SampleCount>
struct SampleRange
{
	std::array<TSampleType, SampleCount> samples = { 0 };
	size_t currentSample = 0;

	void AddSample(TSampleType newSample)
	{
		samples[currentSample++] = newSample;
		if (currentSample >= SampleCount)
		{
			currentSample = 0;
		}

	}

	TSampleType GetAverage()
	{

		TSampleType sampleAverage = 0;
		for (size_t i = 0; i < SampleCount; ++i)
		{
			sampleAverage += samples[i];
		}

		sampleAverage /= SampleCount;
		return sampleAverage;

	}
};