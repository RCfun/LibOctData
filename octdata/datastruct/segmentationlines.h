#pragma once

#include <vector>
#include <array>

#ifdef OCTDATA_EXPORT
	#include "octdata_EXPORTS.h"
#else
	#define Octdata_EXPORTS
#endif


namespace OctData
{

	// GCL IPL INL OPL ELM PR1 PR2 RPE BM
	class Octdata_EXPORTS  Segmentationlines
	{
		static const std::size_t numSegmentlineType = 11;
	public:
		enum class SegmentlineType
		{
			ILM,
			NFL,
			I3T1,
			I4T1,
			I5T1,
			I6T1,
			I8T3,
			I14T1,
			I15T1,
			I16T1,
			BM
		};

		typedef double SegmentlineDataType;
		typedef std::vector<SegmentlineDataType> Segmentline;

		      Segmentline& getSegmentLine(SegmentlineType i)           { return segmentlines.at(static_cast<std::size_t>(i)); }
		const Segmentline& getSegmentLine(SegmentlineType i)     const { return segmentlines.at(static_cast<std::size_t>(i)); }

		static const char* getSegmentlineName(SegmentlineType type);

		constexpr static const std::array<SegmentlineType, numSegmentlineType>& getSegmentlineTypes()
		                                                               { return segmentlineTypes; }
	private:
		static const std::array<SegmentlineType, numSegmentlineType> segmentlineTypes;
		std::array<Segmentline, numSegmentlineType> segmentlines;

	};

}
