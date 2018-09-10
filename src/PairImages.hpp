//============================================================================
// Name        : PairImages.hpp
// Author      : Marcelo Kaihara
// Version     : 1.0
// Copyright   :
// Description : It encapsulates images from the two cameras into one entity.
//============================================================================

#ifndef PAIRIMAGES_HPP_
#define PAIRIMAGES_HPP_

#include "Images.hpp"

namespace ScanVan {

class PairImages {
	Images *p_img0;
	Images *p_img1;

public:
	PairImages();
	PairImages(const Images &a, const Images &b);
	PairImages(const Images &&a, const Images &&b);
	PairImages(const PairImages &a);
	PairImages(PairImages &&a);
	void showPair();
	void showPairConcat();
	void showUndistortPairConcat (const cv::Mat & map_0_1, const cv::Mat & map_0_2, const cv::Mat & map_1_1, const cv::Mat & map_1_2);
	void savePair(std::string path);
	void setImgNumber (const long int &n);
	PairImages & operator=(const PairImages &a);
	PairImages & operator=(PairImages &&a);
	virtual ~PairImages();
};

} /* namespace ScanVan */

#endif /* PAIRIMAGES_HPP_ */
