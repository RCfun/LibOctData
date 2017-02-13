#include "he_e2eread.h"

#include <datastruct/oct.h>
#include <datastruct/coordslo.h>
#include <datastruct/sloimage.h>
#include <datastruct/bscan.h>
#include <filereadoptions.h>

#include <iostream>
#include <fstream>
#include <iomanip>

#include <opencv2/opencv.hpp>

#include <boost/filesystem.hpp>

#include <E2E/e2edata.h>
#include <E2E/dataelements/patientdataelement.h>
#include <E2E/dataelements/image.h>
#include <E2E/dataelements/segmentationdata.h>
#include <E2E/dataelements/bscanmetadataelement.h>
#include <E2E/dataelements/imageregistration.h>
#include <E2E/dataelements/slodataelement.h>
#include <E2E/dataelements/textelement.h>
#include <E2E/dataelements/textelement16.h>
#include <E2E/dataelements/stringlistelement.h>

#include <E2E/dataelements/studydata.h>

#include "he_gray_transform.h"

#include <boost/locale.hpp>
#include <codecvt>
#include <cpp_framework/callback.h>



namespace bfs = boost::filesystem;
namespace loc = boost::locale;


namespace OctData
{

	namespace
	{
		template <class Facet>
		class UsableFacet : public Facet
		{
		public:
			using Facet::Facet; // inherit constructors
			~UsableFacet() {}

			// workaround for compilers without inheriting constructors:
			// template <class ...Args> UsableFacet(Args&& ...args) : Facet(std::forward<Args>(args)...) {}
		};
		template<typename internT, typename externT, typename stateT>
		using codecvt = UsableFacet<std::codecvt<internT, externT, stateT>>;


		Patient::Sex convertSex(E2E::PatientDataElement::Sex e2eSex)
		{
			switch(e2eSex)
			{
				case E2E::PatientDataElement::Sex::Female:
					return Patient::Sex::Female;
				case E2E::PatientDataElement::Sex::Male:
					return Patient::Sex::Male;
				case E2E::PatientDataElement::Sex::Unknown:
					return Patient::Sex::Unknown;
			}
			return Patient::Sex::Unknown;
		}
		
		void copyPatData(Patient& pat, const E2E::Patient& e2ePat)
		{
			if(e2ePat.getPatientUID())
				pat.setPatientUID(e2ePat.getPatientUID()->getText());

			const E2E::PatientDataElement* e2ePatData = e2ePat.getPatientData();
			if(e2ePatData)
			{
				pat.setForename(e2ePatData->getForename());
				pat.setSurname (e2ePatData->getSurname ());
				pat.setId      (e2ePatData->getId      ());
				pat.setSex     (convertSex(e2ePatData->getSex()));
				pat.setTitle   (e2ePatData->getTitle   ());
				pat.setBirthdate(Date::fromWindowsTimeFormat(e2ePatData->getWinBDate()));
			}

			const E2E::TextElement16* e2eDiagnose = e2ePat.getDiagnose();
			if(e2eDiagnose)
				pat.setDiagnose(e2eDiagnose->getText());
		}
		
		void copyStudyData(Study& study, const E2E::Study& e2eStudy)
		{
			if(e2eStudy.getStudyUID())
				study.setStudyUID(e2eStudy.getStudyUID()->getText());
			
			const E2E::StudyData* e2eStudyData = e2eStudy.getStudyData();
			if(e2eStudyData)
			{
				study.setStudyDate(Date::fromWindowsTimeFormat(e2eStudyData->getWindowsStudyDate()));
				study.setStudyOperator(loc::conv::to_utf<char>(e2eStudyData->getOperator(), "ISO-8859-15"));
			}
		}
		
		void copySeriesData(Series& series, const E2E::Series& e2eSeries)
		{
			if(e2eSeries.getSeriesUID())
				series.setSeriesUID(e2eSeries.getSeriesUID()->getText());

// 			std::wstring_convert<std::codecvt<char16_t,char,std::mbstate_t>,char16_t> convert;
			std::wstring_convert<codecvt<char16_t, char, std::mbstate_t>, char16_t> convert;
			
			if(e2eSeries.getExaminedStructure())
			{
				if(e2eSeries.getExaminedStructure()->size() > 0)
				{
					const std::u16string& examinedStructure = e2eSeries.getExaminedStructure()->getString(0);
					if(examinedStructure == u"ONH")
						series.setExaminedStructure(Series::ExaminedStructure::ONH);
					else if(examinedStructure == u"Retina")
						series.setExaminedStructure(Series::ExaminedStructure::Retina);
					else
					{
						series.setExaminedStructure(Series::ExaminedStructure::Unknown);
						series.setExaminedStructureText(convert.to_bytes(examinedStructure));
					}

				}
			}

			if(e2eSeries.getScanPattern())
			{
				if(e2eSeries.getScanPattern()->size() > 0)
				{
					const std::u16string& scanPattern = e2eSeries.getScanPattern()->getString(0);
					if(scanPattern == u"OCT ART Volume")
						series.setScanPattern(Series::ScanPattern::Volume);
					else if(scanPattern == u"OCT Radial+Circles")
						series.setScanPattern(Series::ScanPattern::RadialCircles);
					else
					{
						series.setScanPattern(Series::ScanPattern::Unknown);
						series.setScanPatternText(convert.to_bytes(scanPattern));
					}

				}
			}
			// e2eSeries.
		}

		void copySlo(Series& series, const E2E::Series& e2eSeries, const FileReadOptions& op)
		{
			const E2E::Image* e2eSlo = e2eSeries.getSloImage();
			if(!e2eSlo)
				return;

			SloImage* slo = new SloImage;

			const cv::Mat e2eSloImage = e2eSlo->getImage();

			bool imageIsSet = false;
			if(op.rotateSlo)
			{
				E2E::SloDataElement* e2eSloData = e2eSeries.getSloDataElement();
				if(e2eSloData)
				{
					const float* sloTrans = e2eSloData->getTransformData();

					const double a11 = 1./sloTrans[0];
					const double a12 = -sloTrans[1];
					const double a21 = -sloTrans[3];
					const double a22 = 1./sloTrans[4];

					const double b1  = -sloTrans[2] - a12*e2eSloImage.rows/2;
					const double b2  = -sloTrans[5] + a21*e2eSloImage.cols/2;

					CoordTransform sloTransform(a11, a12, a21, a22, b1, b2);

					cv::Mat trans_mat = (cv::Mat_<double>(2,3) << a11, a12, b1, a21, a22, b2);
					// cv::Mat trans_mat = (cv::Mat_<double>(2,3) << 1, 0, shiftY, degree, 1, shiftX - degree*bscanImageConv.cols/2.);

					uint8_t fillValue = 0;
					if(op.fillEmptyPixelWhite)
						fillValue = 255;

					cv::Mat affineResult;
					cv::warpAffine(e2eSloImage, affineResult, trans_mat, e2eSloImage.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(fillValue));
					slo->setImage(affineResult);
					imageIsSet = true;
					slo->setTransform(sloTransform);
				}
			}

			if(!imageIsSet)
			{
				slo->setImage(e2eSloImage);
			}

			slo->setShift(CoordSLOpx(static_cast<double>(e2eSlo->getImageRows())/2.,
			                         static_cast<double>(e2eSlo->getImageCols())/2.));
			series.takeSloImage(slo);
		}

		void addSegData(BScan::Data& bscanData, BScan::SegmentlineType segType, const E2E::BScan::SegmentationMap& e2eSegMap, int index, int type, const E2E::ImageRegistration* reg, std::size_t imagecols)
		{
			const E2E::BScan::SegmentationMap::const_iterator segPair = e2eSegMap.find(E2E::BScan::SegPair(index, type));
			if(segPair != e2eSegMap.end())
			{
				E2E::SegmentationData* segData = segPair->second;
				if(segData)
				{
					typedef std::vector<double> SegDataVec;
					std::size_t numSegData = segData->size();
					SegDataVec segVec(numSegData);
					if(reg)
					{
						double shiftY = -reg->values[3];
						double degree = -reg->values[7];
						double shiftX = -reg->values[9] - degree*static_cast<double>(imagecols)/2.;
						int    shiftXVec = static_cast<int>(std::round(shiftY));
						double pos    = shiftXVec;

						SegDataVec::iterator segVecBegin = segVec.begin();
						E2E::SegmentationData::pointer segDataBegin = segData->begin();

						std::size_t absShiftX = static_cast<std::size_t>(abs(shiftXVec));
						if(numSegData < absShiftX)
							return;

						std::size_t numAssign = numSegData-absShiftX;

						if(shiftXVec < 0)
							segDataBegin -= shiftXVec;
						if(shiftXVec > 0)
							segVecBegin += shiftXVec;

						std::transform(segDataBegin, segDataBegin+numAssign, segVecBegin, [&pos, shiftX, degree](double value) { return value + shiftX + (++pos)*degree; });
					}
					else
						segVec.assign(segData->begin(), segData->end());

					bscanData.segmentlines.at(static_cast<std::size_t>(segType)) = std::move(segVec);
				}
			}
		}

		template<typename T>
		void fillEmptyBroderCols(cv::Mat& image, T broderValue, T fillValue)
		{
			const int cols = image.cols;
			const int rows = image.rows;

			// find left Broder
			int leftEnd = cols;
			for(int row = 0; row<rows; ++row)
			{
				T* it = image.ptr<T>(row);
				for(int col = 0; col < leftEnd; ++col)
				{
					if(*it != broderValue)
					{
						leftEnd = col;
						break;
					}
					++it;
				}
				if(leftEnd == 0)
					break;
			}

			// fill left Broder
			if(leftEnd > 0)
			{
				for(int row = 0; row<rows; ++row)
				{
					T* it = image.ptr<T>(row);
					for(int col = 0; col < leftEnd; ++col)
					{
						*it = fillValue;
						++it;
					}
				}
			}
			else
				if(leftEnd == cols) // empty Image
					return;

			// find right Broder
			int rightEnd = leftEnd;
			for(int row = 0; row<rows; ++row)
			{
				T* it = image.ptr<T>(row, cols-1);
				for(int col = cols-1; col >= rightEnd; --col)
				{
					if(*it != broderValue)
					{
						rightEnd = col;
						break;
					}
					--it;
				}
				if(rightEnd == cols-1)
					break;
			}

			// fill right Broder
			if(rightEnd < cols)
			{
				for(int row = 0; row<rows; ++row)
				{
					T* it = image.ptr<T>(row, rightEnd);
					for(int col = rightEnd; col < cols; ++col)
					{
						*it = fillValue;
						++it;
					}
				}
			}
		}
		
		template<typename SourceType, typename DestType, typename TransformType>
		void useLUTBScan(const cv::Mat& source, cv::Mat& dest)
		{
			dest.create(source.rows, source.cols, cv::DataType<DestType>::type);

			TransformType& lut = TransformType::getInstance();
			
			const SourceType* sPtr = source.ptr<SourceType>();
			DestType* dPtr = dest.ptr<DestType>();
			
			const std::size_t size = source.cols * source.rows;
			for(size_t i = 0; i < size; ++i)
			{
				*dPtr = lut.getValue(*sPtr);
				++dPtr;
				++sPtr;
			}
		}

		void copyBScan(Series& series, const E2E::BScan& e2eBScan, const FileReadOptions& op)
		{
			const E2E::Image* e2eBScanImg = e2eBScan.getImage();
			if(!e2eBScanImg)
				return;

			const cv::Mat& e2eImage = e2eBScanImg->getImage();


			BScan::Data bscanData;

			const E2E::BScanMetaDataElement* e2eMeta = e2eBScan.getBScanMetaDataElement();
			if(e2eMeta)
			{
				uint32_t factor = 30; // TODO
				bscanData.start = CoordSLOmm(e2eMeta->getX1()*factor, e2eMeta->getY1()*factor);
				bscanData.end   = CoordSLOmm(e2eMeta->getX2()*factor, e2eMeta->getY2()*factor);

				bscanData.numAverage      = e2eMeta->getNumAve();
				bscanData.imageQuality    = e2eMeta->getImageQuality();
				bscanData.acquisitionTime = Date::fromWindowsTicks(e2eMeta->getAcquisitionTime());

				if(e2eMeta->getScanType() == E2E::BScanMetaDataElement::ScanType::Circle)
					bscanData.center = CoordSLOmm(e2eMeta->getCenterX()*factor, e2eMeta->getCenterY()*factor);
			}


			const E2E::ImageRegistration* reg = nullptr;
			if(op.registerBScanns)
				reg = e2eBScan.getImageRegistrationData();

			// segmenation lines
			const E2E::BScan::SegmentationMap& e2eSegMap = e2eBScan.getSegmentationMap();
			addSegData(bscanData, BScan::SegmentlineType::ILM, e2eSegMap, 0, 5, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::BM , e2eSegMap, 1, 2, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::NFL, e2eSegMap, 2, 7, reg, e2eImage.cols);
			
			addSegData(bscanData, BScan::SegmentlineType::I3T1 , e2eSegMap,  3, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I4T1 , e2eSegMap,  4, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I5T1 , e2eSegMap,  5, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I6T1 , e2eSegMap,  6, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I8T3 , e2eSegMap,  8, 3, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I14T1, e2eSegMap, 14, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I15T1, e2eSegMap, 15, 1, reg, e2eImage.cols);
			addSegData(bscanData, BScan::SegmentlineType::I16T1, e2eSegMap, 16, 1, reg, e2eImage.cols);

			cv::Mat bscanImageConv;
			if(e2eImage.type() == cv::DataType<float>::type)
			{
				cv::Mat bscanImagePow;
				cv::pow(e2eImage, 0.25, bscanImagePow);
				bscanImagePow.convertTo(bscanImageConv, CV_8U, 255, 0);
			}
			else
			{
				cv::Mat dest;
				// convert image
				switch(op.e2eGray)
				{
				case FileReadOptions::E2eGrayTransform::nativ:
					e2eImage.convertTo(dest, CV_32FC1, 1/static_cast<double>(1 << 16), 0);
					cv::pow(dest, 8, dest);
					dest.convertTo(bscanImageConv, CV_8U, 255, 0);
					break;
				case FileReadOptions::E2eGrayTransform::xml:
					useLUTBScan<uint16_t, uint8_t, HeGrayTransformXml>(e2eImage, bscanImageConv);
					break;
				case FileReadOptions::E2eGrayTransform::vol:
					useLUTBScan<uint16_t, uint8_t, HeGrayTransformVol>(e2eImage, bscanImageConv);
					break;
				}
				if(bscanImageConv.empty())
				{
					std::cerr << "Error: Converted Matrix empty, valid options?\n";
					useLUTBScan<uint16_t, uint8_t, HeGrayTransformXml>(e2eImage, bscanImageConv);
				}
			}

			if(!op.fillEmptyPixelWhite)
				fillEmptyBroderCols<uint8_t>(bscanImageConv, 255, 0);

			// testcode
			if(reg)
			{
				// std::cout << "shift X: " << reg->values[9] << std::endl;
				double shiftY = -reg->values[3];
				double degree = -reg->values[7];
				double shiftX = -reg->values[9];
				// std::cout << "shift X: " << shiftX << "\tdegree: " << degree << "\t" << (degree*bscanImageConv.cols/2) << std::endl;
				cv::Mat trans_mat = (cv::Mat_<double>(2,3) << 1, 0, shiftY, degree, 1, shiftX - degree*bscanImageConv.cols/2.);

				uint8_t fillValue = 0;
				if(op.fillEmptyPixelWhite)
					fillValue = 255;
				cv::warpAffine(bscanImageConv, bscanImageConv, trans_mat, bscanImageConv.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(fillValue));
			}

			BScan* bscan = new BScan(bscanImageConv, bscanData);
			if(op.holdRawData)
				bscan->setRawImage(e2eImage);
			series.takeBScan(bscan);
		}
	}


	HeE2ERead::HeE2ERead()
	: OctFileReader({OctExtension(".E2E", "Heidelberg Engineering E2E File"), OctExtension(".sdb", "Heidelberg Engineering HEYEX File")})
	{
	}

	bool HeE2ERead::readFile(const boost::filesystem::path& file, OCT& oct, const FileReadOptions& op, CppFW::Callback* callback)
	{
		if(file.extension() != ".E2E" && file.extension() != ".sdb")
			return false;

		if(!bfs::exists(file))
			return false;

		CppFW::Callback loadCallback   ;
		CppFW::Callback convertCallback;

		if(callback)
		{
			loadCallback    = callback->createSubTask(0.5, 0.0);
			convertCallback = callback->createSubTask(0.5, 0.5);
		}

		E2E::E2EData e2eData;
		e2eData.readE2EFile(file.generic_string(), &loadCallback);

		const E2E::DataRoot& e2eRoot = e2eData.getDataRoot();

		std::size_t basisFileId = e2eRoot.getCreateFromLoadedFileNum();


		// load extra Data from patient file (pdb) and study file (edb)
		if(file.extension() == ".sdb")
		{
			for(const E2E::DataRoot::SubstructurePair& e2ePatPair : e2eRoot)
			{
				const std::size_t bufferSize = 100;
				char buffer[bufferSize];
				const E2E::Patient& e2ePat = *(e2ePatPair.second);
				std::snprintf(buffer, bufferSize, "%08d.pdb", e2ePatPair.first);
				// std::string filenname =
				bfs::path patientDataFile(file.branch_path() / buffer);
				if(bfs::exists(patientDataFile))
					e2eData.readE2EFile(patientDataFile.generic_string());

				for(const E2E::Patient::SubstructurePair& e2eStudyPair : e2ePat)
				{
					std::snprintf(buffer, bufferSize, "%08d.edb", e2eStudyPair.first);
					bfs::path studyDataFile(file.branch_path() / buffer);
					if(bfs::exists(studyDataFile))
						e2eData.readE2EFile(studyDataFile.generic_string());
				}
			}
		}

		CppFW::CallbackSubTaskCreator callbackCreatorPatients(&convertCallback, e2eRoot.size());
		// convert e2e structure in octdata structure
		for(const E2E::DataRoot::SubstructurePair& e2ePatPair : e2eRoot)
		{
			const E2E::Patient& e2ePat = *(e2ePatPair.second);

			CppFW::Callback callbackPatient = callbackCreatorPatients.getSubTaskCallback();
			CppFW::CallbackSubTaskCreator callbackCreatorStudys(&callbackPatient, e2ePat.size());

			if(e2ePat.getCreateFromLoadedFileNum() != basisFileId)
				continue;

			Patient& pat = oct.getPatient(e2ePatPair.first);
			copyPatData(pat, e2ePat);
			
			for(const E2E::Patient::SubstructurePair& e2eStudyPair : e2ePat)
			{
				const E2E::Study& e2eStudy = *(e2eStudyPair.second);

				CppFW::Callback callbackStudy = callbackCreatorStudys.getSubTaskCallback();
				CppFW::CallbackSubTaskCreator callbackCreatorSeries(&callbackPatient, e2eStudy.size());

				if(e2eStudy.getCreateFromLoadedFileNum() != basisFileId)
					continue;

// 				std::cout << "studyID: " << studyID << std::endl;
				Study& study = pat.getStudy(e2eStudyPair.first);

				copyStudyData(study, e2eStudy);

				
				for(const E2E::Study::SubstructurePair& e2eSeriesPair : e2eStudy)
				{
					CppFW::Callback callbackSeries = callbackCreatorSeries.getSubTaskCallback();

					const E2E::Series& e2eSeries = *(e2eSeriesPair.second);
					if(e2eSeries.getCreateFromLoadedFileNum() != basisFileId)
						continue;

// 					std::cout << "seriesID: " << seriesID << std::endl;
					Series& series = study.getSeries(e2eSeriesPair.first);
					copySlo(series, e2eSeries, op);
					
					copySeriesData(series, e2eSeries);

					CppFW::CallbackStepper bscanCallbackStepper(&callbackSeries, e2eSeries.size());
					for(const E2E::Series::SubstructurePair& e2eBScanPair : e2eSeries)
					{
						copyBScan(series, *(e2eBScanPair.second), op);
						++bscanCallbackStepper;
					}
				}
			}
		}

		return true;
	}

	HeE2ERead* HeE2ERead::getInstance()
	{
		static HeE2ERead instance; return &instance;
	}


}
