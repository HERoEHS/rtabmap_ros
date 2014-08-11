/*
 * Copyright (C) 2010-2011, Mathieu Labbe and IntRoLab - Universite de Sherbrooke
 *
 * This file is part of RTAB-Map.
 *
 * RTAB-Map is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RTAB-Map is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAB-Map.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "rtabmap/core/Features2d.h"
#include "rtabmap/core/util3d.h"
#include "rtabmap/utilite/UStl.h"
#include "rtabmap/utilite/UConversion.h"
#include "rtabmap/utilite/ULogger.h"
#include "rtabmap/utilite/UMath.h"
#include "rtabmap/utilite/ULogger.h"
#include "rtabmap/utilite/UTimer.h"
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/gpu/gpu.hpp>
#include <opencv2/core/version.hpp>

#if CV_MAJOR_VERSION >=2 && CV_MINOR_VERSION >=4
#include <opencv2/nonfree/gpu.hpp>
#include <opencv2/nonfree/features2d.hpp>
#endif

#define OPENCV_SURF_GPU CV_MAJOR_VERSION >= 2 and CV_MINOR_VERSION >=2 and CV_SUBMINOR_VERSION>=1

namespace rtabmap {

void filterKeypointsByDepth(
		std::vector<cv::KeyPoint> & keypoints,
		const cv::Mat & depth,
		float fx,
		float fy,
		float cx,
		float cy,
		float maxDepth)
{
	cv::Mat descriptors;
	filterKeypointsByDepth(keypoints, descriptors, depth, fx, fy, cx, cy, maxDepth);
}

void filterKeypointsByDepth(
		std::vector<cv::KeyPoint> & keypoints,
		cv::Mat & descriptors,
		const cv::Mat & depth,
		float fx,
		float fy,
		float cx,
		float cy,
		float maxDepth)
{
	if(!depth.empty() && fx > 0.0f && fy > 0.0f && maxDepth > 0.0f && (descriptors.empty() || descriptors.rows == (int)keypoints.size()))
	{
		std::vector<cv::KeyPoint> output(keypoints.size());
		std::vector<int> indexes(keypoints.size(), 0);
		int oi=0;
		for(unsigned int i=0; i<keypoints.size(); ++i)
		{
			pcl::PointXYZ pt = util3d::getDepth(depth, keypoints[i].pt.x, keypoints[i].pt.y, cx, cy, fx, fy, true);
			if(uIsFinite(pt.z) && pt.z < maxDepth)
			{
				output[oi++] = keypoints[i];
				indexes[i] = 1;
			}
		}
		output.resize(oi);
		keypoints = output;

		if(!descriptors.empty() && (int)keypoints.size() != descriptors.rows)
		{
			if(keypoints.size() == 0)
			{
				descriptors = cv::Mat();
			}
			else
			{
				cv::Mat newDescriptors(keypoints.size(), descriptors.cols, descriptors.type());
				int di = 0;
				for(unsigned int i=0; i<indexes.size(); ++i)
				{
					if(indexes[i] == 1)
					{
						memcpy(newDescriptors.ptr<float>(di++), descriptors.ptr<float>(i), descriptors.cols*sizeof(float));
					}
				}
				descriptors = newDescriptors;
			}
		}
	}
}

void limitKeypoints(std::vector<cv::KeyPoint> & keypoints, int maxKeypoints)
{
	cv::Mat descriptors;
	limitKeypoints(keypoints, descriptors, maxKeypoints);
}

void limitKeypoints(std::vector<cv::KeyPoint> & keypoints, cv::Mat & descriptors, int maxKeypoints)
{
	UASSERT((int)keypoints.size() == descriptors.rows || descriptors.rows == 0);
	if(maxKeypoints > 0 && (int)keypoints.size() > maxKeypoints)
	{
		UTimer timer;
		ULOGGER_DEBUG("too much words (%d), removing words with the hessian threshold", keypoints.size());
		// Remove words under the new hessian threshold

		// Sort words by hessian
		std::multimap<float, int> hessianMap; // <hessian,id>
		for(unsigned int i = 0; i <keypoints.size(); ++i)
		{
			//Keep track of the data, to be easier to manage the data in the next step
			hessianMap.insert(std::pair<float, int>(fabs(keypoints[i].response), i));
		}

		// Remove them from the signature
		int removed = hessianMap.size()-maxKeypoints;
		std::multimap<float, int>::reverse_iterator iter = hessianMap.rbegin();
		std::vector<cv::KeyPoint> kptsTmp(maxKeypoints);
		cv::Mat descriptorsTmp;
		if(descriptors.rows)
		{
			descriptorsTmp = cv::Mat(maxKeypoints, descriptors.cols, descriptors.type());
		}
		for(unsigned int k=0; k < kptsTmp.size() && iter!=hessianMap.rend(); ++k, ++iter)
		{
			kptsTmp[k] = keypoints[iter->second];
			if(descriptors.rows)
			{
				memcpy(descriptorsTmp.ptr<float>(k), descriptors.ptr<float>(iter->second), descriptors.cols*sizeof(float));
			}
		}
		ULOGGER_DEBUG("%d keypoints removed, (kept %d), minimum response=%f", removed, keypoints.size(), kptsTmp.size()?kptsTmp.back().response:0.0f);
		ULOGGER_DEBUG("removing words time = %f s", timer.ticks());
		keypoints = kptsTmp;
		if(descriptors.rows)
		{
			descriptors = descriptorsTmp;
		}
	}
}

cv::Rect computeRoi(const cv::Mat & image, const std::vector<float> & roiRatios)
{
	if(!image.empty() && roiRatios.size() == 4)
	{
		float width = image.cols;
		float height = image.rows;
		cv::Rect roi(0, 0, width, height);
		UDEBUG("roi ratios = %f, %f, %f, %f", roiRatios[0],roiRatios[1],roiRatios[2],roiRatios[3]);
		UDEBUG("roi = %d, %d, %d, %d", roi.x, roi.y, roi.width, roi.height);

		//left roi
		if(roiRatios[0] > 0 && roiRatios[0] < 1 - roiRatios[1])
		{
			roi.x = width * roiRatios[0];
		}

		//right roi
		roi.width = width - roi.x;
		if(roiRatios[1] > 0 && roiRatios[1] < 1 - roiRatios[0])
		{
			roi.width -= width * roiRatios[1];
		}

		//top roi
		if(roiRatios[2] > 0 && roiRatios[2] < 1 - roiRatios[3])
		{
			roi.y = height * roiRatios[2];
		}

		//bottom roi
		roi.height = height - roi.y;
		if(roiRatios[3] > 0 && roiRatios[3] < 1 - roiRatios[2])
		{
			roi.height -= height * roiRatios[3];
		}
		UDEBUG("roi = %d, %d, %d, %d", roi.x, roi.y, roi.width, roi.height);

		return roi;
	}
	else
	{
		UERROR("Image is null or _roiRatios(=%d) != 4", roiRatios.size());
		return cv::Rect();
	}
}

/////////////////////
// Feature2D
/////////////////////
std::vector<cv::KeyPoint> Feature2D::generateKeypoints(const cv::Mat & image, int maxKeypoints, const cv::Rect & roi) const
{
	ULOGGER_DEBUG("");
	std::vector<cv::KeyPoint> keypoints;
	if(!image.empty() && image.channels() == 1 && image.type() == CV_8U)
	{
		UTimer timer;

		// Get keypoints
		keypoints = this->generateKeypointsImpl(image, roi.width && roi.height?roi:cv::Rect(0,0,image.cols, image.rows));
		ULOGGER_DEBUG("Keypoints extraction time = %f s, keypoints extracted = %d", timer.ticks(), keypoints.size());

		limitKeypoints(keypoints, maxKeypoints);

		if(roi.x || roi.y)
		{
			// Adjust keypoint position to raw image
			for(std::vector<cv::KeyPoint>::iterator iter=keypoints.begin(); iter!=keypoints.end(); ++iter)
			{
				iter->pt.x += roi.x;
				iter->pt.y += roi.y;
			}
		}
	}
	else if(image.empty())
	{
		UERROR("Image is null!");
	}
	else
	{
		UERROR("Image format must be mono8. Current has %d channels and type = %d, size=%d,%d",
				image.channels(), image.type(), image.cols, image.rows);
	}

	return keypoints;
}

cv::Mat Feature2D::generateDescriptors(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	return generateDescriptorsImpl(image, keypoints);
}

//////////////////////////
//SURF
//////////////////////////
SURF::SURF(const ParametersMap & parameters) :
		hessianThreshold_(Parameters::defaultSURFHessianThreshold()),
		nOctaves_(Parameters::defaultSURFOctaves()),
		nOctaveLayers_(Parameters::defaultSURFOctaveLayers()),
		extended_(Parameters::defaultSURFExtended()),
		upright_(Parameters::defaultSURFUpright()),
		gpuKeypointsRatio_(Parameters::defaultSURFGpuKeypointsRatio()),
		gpuVersion_(Parameters::defaultSURFGpuVersion()),
		_surf(0),
		_gpuSurf(0)
{
	parseParameters(parameters);
}

SURF::~SURF()
{
	if(_surf)
	{
		delete _surf;
	}
	if(_gpuSurf)
	{
		delete _gpuSurf;
	}
}

void SURF::parseParameters(const ParametersMap & parameters)
{
	Parameters::parse(parameters, Parameters::kSURFExtended(), extended_);
	Parameters::parse(parameters, Parameters::kSURFHessianThreshold(), hessianThreshold_);
	Parameters::parse(parameters, Parameters::kSURFOctaveLayers(), nOctaveLayers_);
	Parameters::parse(parameters, Parameters::kSURFOctaves(), nOctaves_);
	Parameters::parse(parameters, Parameters::kSURFUpright(), upright_);
	Parameters::parse(parameters, Parameters::kSURFGpuKeypointsRatio(), gpuKeypointsRatio_);
	Parameters::parse(parameters, Parameters::kSURFGpuVersion(), gpuVersion_);

	if(_gpuSurf)
	{
		delete _gpuSurf;
		_gpuSurf = 0;
	}
	if(_surf)
	{
		delete _surf;
		_surf = 0;
	}

	if(gpuVersion_ && cv::gpu::getCudaEnabledDeviceCount())
	{
		_gpuSurf = new cv::gpu::SURF_GPU(hessianThreshold_, nOctaves_, nOctaveLayers_, extended_, gpuKeypointsRatio_, upright_);
	}
	else
	{
		if(gpuVersion_)
		{
			UWARN("GPU version of SURF not available! Using CPU version instead...");
		}

		_surf = new cv::SURF(hessianThreshold_, nOctaves_, nOctaveLayers_, extended_, upright_);
	}
}

std::vector<cv::KeyPoint> SURF::generateKeypointsImpl(const cv::Mat & image, const cv::Rect & roi) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	std::vector<cv::KeyPoint> keypoints;
	cv::Mat imgRoi(image, roi);
	if(_gpuSurf)
	{
		cv::gpu::GpuMat imgGpu(imgRoi);
		(*_gpuSurf)(imgGpu, cv::gpu::GpuMat(), keypoints);
	}
	else
	{
		_surf->detect(imgRoi, keypoints);
	}

	return keypoints;
}

cv::Mat SURF::generateDescriptorsImpl(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	cv::Mat descriptors;
	if(_gpuSurf)
	{
		cv::gpu::GpuMat imgGpu(image);
		cv::gpu::GpuMat descriptorsGPU;
		(*_gpuSurf)(imgGpu, cv::gpu::GpuMat(), keypoints, descriptorsGPU, true);

		// Download descriptors
		if (descriptorsGPU.empty())
			descriptors = cv::Mat();
		else
		{
			UASSERT(descriptorsGPU.type() == CV_32F);
			descriptors = cv::Mat(descriptorsGPU.size(), CV_32F);
			descriptorsGPU.download(descriptors);
		}
	}
	else
	{
		_surf->compute(image, keypoints, descriptors);
	}

	return descriptors;
}

//////////////////////////
//SIFT
//////////////////////////
SIFT::SIFT(const ParametersMap & parameters) :
	nfeatures_(Parameters::defaultSIFTNFeatures()),
	nOctaveLayers_(Parameters::defaultSIFTNOctaveLayers()),
	contrastThreshold_(Parameters::defaultSIFTContrastThreshold()),
	edgeThreshold_(Parameters::defaultSIFTEdgeThreshold()),
	sigma_(Parameters::defaultSIFTSigma()),
	_sift(0)
{
	parseParameters(parameters);
}

SIFT::~SIFT()
{
	if(_sift)
	{
		delete _sift;
	}
}

void SIFT::parseParameters(const ParametersMap & parameters)
{
	Parameters::parse(parameters, Parameters::kSIFTContrastThreshold(), contrastThreshold_);
	Parameters::parse(parameters, Parameters::kSIFTEdgeThreshold(), edgeThreshold_);
	Parameters::parse(parameters, Parameters::kSIFTNFeatures(), nfeatures_);
	Parameters::parse(parameters, Parameters::kSIFTNOctaveLayers(), nOctaveLayers_);
	Parameters::parse(parameters, Parameters::kSIFTSigma(), sigma_);

	if(_sift)
	{
		delete _sift;
		_sift = 0;
	}

	_sift = new cv::SIFT(nfeatures_, nOctaveLayers_, contrastThreshold_, edgeThreshold_, sigma_);
}

std::vector<cv::KeyPoint> SIFT::generateKeypointsImpl(const cv::Mat & image, const cv::Rect & roi) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	std::vector<cv::KeyPoint> keypoints;
	cv::Mat imgRoi(image, roi);
	_sift->detect(imgRoi, keypoints); // Opencv keypoints
	return keypoints;
}

cv::Mat SIFT::generateDescriptorsImpl(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	cv::Mat descriptors;
	_sift->compute(image, keypoints, descriptors);
	return descriptors;
}

//////////////////////////
//ORB
//////////////////////////
ORB::ORB(const ParametersMap & parameters) :
		nFeatures_(Parameters::defaultORBNFeatures()),
		scaleFactor_(Parameters::defaultORBScaleFactor()),
		nLevels_(Parameters::defaultORBNLevels()),
		edgeThreshold_(Parameters::defaultORBEdgeThreshold()),
		firstLevel_(Parameters::defaultORBFirstLevel()),
		WTA_K_(Parameters::defaultORBWTA_K()),
		scoreType_(Parameters::defaultORBScoreType()),
		patchSize_(Parameters::defaultORBPatchSize()),
		gpu_(Parameters::defaultORBGpu()),
		fastThreshold_(Parameters::defaultFASTThreshold()),
		nonmaxSuppresion_(Parameters::defaultFASTNonmaxSuppression()),
		_orb(0),
		_gpuOrb(0)
{
	parseParameters(parameters);
}

ORB::~ORB()
{
	if(_orb)
	{
		delete _orb;
	}
	if(_gpuOrb)
	{
		delete _gpuOrb;
	}
}

void ORB::parseParameters(const ParametersMap & parameters)
{
	Parameters::parse(parameters, Parameters::kORBNFeatures(), nFeatures_);
	Parameters::parse(parameters, Parameters::kORBScaleFactor(), scaleFactor_);
	Parameters::parse(parameters, Parameters::kORBNLevels(), nLevels_);
	Parameters::parse(parameters, Parameters::kORBEdgeThreshold(), edgeThreshold_);
	Parameters::parse(parameters, Parameters::kORBFirstLevel(), firstLevel_);
	Parameters::parse(parameters, Parameters::kORBWTA_K(), WTA_K_);
	Parameters::parse(parameters, Parameters::kORBScoreType(), scoreType_);
	Parameters::parse(parameters, Parameters::kORBPatchSize(), patchSize_);
	Parameters::parse(parameters, Parameters::kORBGpu(), gpu_);

	Parameters::parse(parameters, Parameters::kFASTThreshold(), fastThreshold_);
	Parameters::parse(parameters, Parameters::kFASTNonmaxSuppression(), nonmaxSuppresion_);

	if(_gpuOrb)
	{
		delete _gpuOrb;
		_gpuOrb = 0;
	}
	if(_orb)
	{
		delete _orb;
		_orb = 0;
	}

	if(gpu_ && cv::gpu::getCudaEnabledDeviceCount())
	{
		_gpuOrb = new cv::gpu::ORB_GPU(nFeatures_, scaleFactor_, nLevels_, edgeThreshold_, firstLevel_, WTA_K_, scoreType_, patchSize_);
		_gpuOrb->setFastParams(fastThreshold_, nonmaxSuppresion_);
	}
	else
	{
		if(gpu_)
		{
			UWARN("GPU version of ORB not available! Using CPU version instead...");
		}
		_orb = new cv::ORB(nFeatures_, scaleFactor_, nLevels_, edgeThreshold_, firstLevel_, WTA_K_, scoreType_, patchSize_);
	}
}

std::vector<cv::KeyPoint> ORB::generateKeypointsImpl(const cv::Mat & image, const cv::Rect & roi) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	std::vector<cv::KeyPoint> keypoints;
	cv::Mat imgRoi(image, roi);
	if(_gpuOrb)
	{
		cv::gpu::GpuMat imgGpu(imgRoi);
		(*_gpuOrb)(imgGpu, cv::gpu::GpuMat(), keypoints);
	}
	else
	{
		_orb->detect(imgRoi, keypoints);
	}

	return keypoints;
}

cv::Mat ORB::generateDescriptorsImpl(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	cv::Mat descriptors;
	if(image.empty())
	{
		ULOGGER_ERROR("Image is null ?!?");
		return descriptors;
	}
	if(_gpuOrb)
	{
		cv::gpu::GpuMat imgGpu(image);
		cv::gpu::GpuMat descriptorsGPU;
		(*_gpuOrb)(imgGpu, cv::gpu::GpuMat(), keypoints, descriptorsGPU);

		// Download descriptors
		if (descriptorsGPU.empty())
			descriptors = cv::Mat();
		else
		{
			UASSERT(descriptorsGPU.type() == CV_32F);
			descriptors = cv::Mat(descriptorsGPU.size(), CV_32F);
			descriptorsGPU.download(descriptors);
		}
	}
	else
	{
		_orb->compute(image, keypoints, descriptors);
	}

	return descriptors;
}

//////////////////////////
//FAST
//////////////////////////
FAST::FAST(const ParametersMap & parameters) :
		threshold_(Parameters::defaultFASTThreshold()),
		nonmaxSuppression_(Parameters::defaultFASTNonmaxSuppression()),
		gpu_(Parameters::defaultFASTGpu()),
		gpuKeypointsRatio_(Parameters::defaultFASTGpuKeypointsRatio()),
		_fast(0),
		_gpuFast(0)
{
	parseParameters(parameters);
}

FAST::~FAST()
{
	if(_fast)
	{
		delete _fast;
	}
	if(_gpuFast)
	{
		delete _gpuFast;
	}
}

void FAST::parseParameters(const ParametersMap & parameters)
{
	Parameters::parse(parameters, Parameters::kFASTThreshold(), threshold_);
	Parameters::parse(parameters, Parameters::kFASTNonmaxSuppression(), nonmaxSuppression_);
	Parameters::parse(parameters, Parameters::kFASTGpu(), gpu_);
	Parameters::parse(parameters, Parameters::kFASTGpuKeypointsRatio(), gpuKeypointsRatio_);

	if(_gpuFast)
	{
		delete _gpuFast;
		_gpuFast = 0;
	}
	if(_fast)
	{
		delete _fast;
		_fast = 0;
	}

	if(gpu_ && cv::gpu::getCudaEnabledDeviceCount())
	{
		_gpuFast = new cv::gpu::FAST_GPU(threshold_, nonmaxSuppression_, gpuKeypointsRatio_);
	}
	else
	{
		if(gpu_)
		{
			UWARN("GPU version of FAST not available! Using CPU version instead...");
		}
		_fast = new cv::FastFeatureDetector(threshold_, nonmaxSuppression_);
	}
}

std::vector<cv::KeyPoint> FAST::generateKeypointsImpl(const cv::Mat & image, const cv::Rect & roi) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	std::vector<cv::KeyPoint> keypoints;
	cv::Mat imgRoi(image, roi);
	if(_gpuFast)
	{
		cv::gpu::GpuMat imgGpu(imgRoi);
		(*_gpuFast)(imgGpu, cv::gpu::GpuMat(), keypoints);
	}
	else
	{
		_fast->detect(imgRoi, keypoints); // Opencv keypoints
	}
	return keypoints;
}

//////////////////////////
//FAST-BRIEF
//////////////////////////
FAST_BRIEF::FAST_BRIEF(const ParametersMap & parameters) :
	FAST(parameters),
	bytes_(Parameters::defaultBRIEFBytes()),
	_brief(0)
{
	parseParameters(parameters);
}

FAST_BRIEF::~FAST_BRIEF()
{
	if(_brief)
	{
		delete _brief;
	}
}

void FAST_BRIEF::parseParameters(const ParametersMap & parameters)
{
	FAST::parseParameters(parameters);

	Parameters::parse(parameters, Parameters::kBRIEFBytes(), bytes_);
	if(_brief)
	{
		delete _brief;
		_brief = 0;
	}
	_brief = new cv::BriefDescriptorExtractor(bytes_);
}

cv::Mat FAST_BRIEF::generateDescriptorsImpl(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	cv::Mat descriptors;
	_brief->compute(image, keypoints, descriptors);
	return descriptors;
}

//////////////////////////
//FAST-FREAK
//////////////////////////
FAST_FREAK::FAST_FREAK(const ParametersMap & parameters) :
	FAST(parameters),
	orientationNormalized_(Parameters::defaultFREAKOrientationNormalized()),
	scaleNormalized_(Parameters::defaultFREAKScaleNormalized()),
	patternScale_(Parameters::defaultFREAKPatternScale()),
	nOctaves_(Parameters::defaultFREAKNOctaves()),
	_freak(0)
{
	parseParameters(parameters);
}

FAST_FREAK::~FAST_FREAK()
{
	if(_freak)
	{
		delete _freak;
	}
}

void FAST_FREAK::parseParameters(const ParametersMap & parameters)
{
	FAST::parseParameters(parameters);

	Parameters::parse(parameters, Parameters::kFREAKOrientationNormalized(), orientationNormalized_);
	Parameters::parse(parameters, Parameters::kFREAKScaleNormalized(), scaleNormalized_);
	Parameters::parse(parameters, Parameters::kFREAKPatternScale(), patternScale_);
	Parameters::parse(parameters, Parameters::kFREAKNOctaves(), nOctaves_);

	if(_freak)
	{
		delete _freak;
		_freak = 0;
	}

	_freak = new cv::FREAK(orientationNormalized_, scaleNormalized_, patternScale_, nOctaves_);
}

cv::Mat FAST_FREAK::generateDescriptorsImpl(const cv::Mat & image, std::vector<cv::KeyPoint> & keypoints) const
{
	UASSERT(!image.empty() && image.channels() == 1 && image.depth() == CV_8U);
	cv::Mat descriptors;
	_freak->compute(image, keypoints, descriptors);
	return descriptors;
}

}
