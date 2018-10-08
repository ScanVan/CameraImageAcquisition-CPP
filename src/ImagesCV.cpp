#include "ImagesCV.hpp"

namespace ScanVan {

ImagesCV::ImagesCV(ImagesRaw &img): Images{} {
	cv::Mat openCvImageRG8 = cv::Mat(height, width, CV_8UC1, img.getBufferP());
	cv::cvtColor(openCvImageRG8, openCvImage, cv::COLOR_BayerRG2RGB);

	height = img.getHeight();
	width = img.getWidth();
	cameraIdx = img.getCameraIdx();	// camera index
	captureTimeCPUStr = img.getCaptureCPUTime(); // capture time taken on the CPU in string format
	captureTimeCamStr = img.getCaptureCamTime(); // trigger time retrieved from the camera, number of ticks, string format
	exposureTime = img.getExposureTime();// exposure time
	gain = img.getGain();		// gain
	balanceR = img.getBalanceR();	// white balance R
	balanceG = img.getBalanceG();	// white balance G
	balanceB = img.getBalanceB();	// white balance B
	autoExpTime = img.getAutoExpTime();	// Auto Exposure Time
	autoGain = img.getAutoGain(); 		// Auto Gain
	numImages = img.getImgNumber();
	serialNum = img.getSerialNumber();
}

void ImagesCV::show () const {
	/// Display
	cv::namedWindow("ImageCV", cv::WINDOW_NORMAL);
	cv::imshow("ImageCV", openCvImage);
};

void ImagesCV::show(std::string name) const {
	/// Display
	cv::namedWindow(name, cv::WINDOW_NORMAL);
	cv::imshow(name, openCvImage);
};

ImagesCV::~ImagesCV() {
	// TODO Auto-generated destructor stub
}

} /* namespace ScanVan */
