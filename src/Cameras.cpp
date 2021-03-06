//============================================================================
// Name        : Cameras.cpp
// Author      : Marcelo Kaihara
// Version     : 1.0
// Copyright   :
// Description : It initializes the camera, sets the configuration of the cameras
//				 from files. It gives the main interface to operate with the cameras
//				 GrabImages, DisplayImages and StoreImages are the main member functions
//				 that are executed in parallel.
//============================================================================

#include "Cameras.hpp"

// The code assumes there are two cameras connected

// Settings to use Basler GigE cameras.
using namespace Basler_GigECameraParams;
// Namespace for using pylon objects.
using namespace Pylon;
// Namespace for using cout.
using namespace std;

namespace ScanVan {

Cameras::Cameras() {
	// It will not load the configuration file to the camera
	loadParam = false;
	// Loads the camera parameters from the file genparam.cfg under the config folder
	LoadCameraConfig();
	Init();
	LoadMap();
}

Cameras::Cameras(std::string path_to_config_files): config_path { path_to_config_files } {
	// It will load the configuration file to the cameras
	// Files are located under the folder path_to_config_files
	// The names of the files are the serial number of the camera .pfs
	loadParam = true;
	// Loads the camera parameters from the file genparam.cfg under the config folder
	LoadCameraConfig();
	Init();
	LoadMap();
}

void Cameras::Init() {

	CTlFactory& tlFactory = CTlFactory::GetInstance();
	pTL = dynamic_cast<IGigETransportLayer*>(tlFactory.CreateTl(BaslerGigEDeviceClass));
	if (pTL == nullptr) {
		throw RUNTIME_EXCEPTION("No GigE transport layer available.");
	}

	// In this sample we use the transport layer directly to enumerate cameras.
	// By calling EnumerateDevices on the TL we get get only GigE cameras.
	// You could also accomplish this by using a filter and
	// let the Transport Layer Factory enumerate.
	DeviceInfoList_t allDeviceInfos { };
	if (pTL->EnumerateDevices(allDeviceInfos) == 0) {
		throw RUNTIME_EXCEPTION("No GigE cameras present.");
	}

	// Only use cameras in the same subnet as the first one.
	DeviceInfoList_t usableDeviceInfos { };
	usableDeviceInfos.push_back(allDeviceInfos[0]);
	subnet = static_cast<const CBaslerGigEDeviceInfo&>(allDeviceInfos[0]).GetSubnetAddress();

	// Start with index 1 as we have already added the first one above.
	// We will also limit the number of cameras to c_maxCamerasToUse.
	for (size_t i = 1; i < allDeviceInfos.size() && usableDeviceInfos.size() < c_maxCamerasToUse; ++i) {
		const CBaslerGigEDeviceInfo& gigeinfo = static_cast<const CBaslerGigEDeviceInfo&>(allDeviceInfos[i]);
		if (subnet == gigeinfo.GetSubnetAddress()) {
			// Add this deviceInfo to the ones we will be using.
			usableDeviceInfos.push_back(gigeinfo);
		} else {
			cerr << "Camera will not be used because it is in a different subnet " << subnet << "!" << endl;
		}
	}

	// Check if all the cameras have been detected
	if (usableDeviceInfos.size() > c_maxCamerasToUse) {
		throw std::runtime_error("More than maxCamerasToUse cameras detected!");
	}
	if (usableDeviceInfos.size() > c_maxCamerasToUse) {
		std::cerr << "Not all the cameras have been detected!" << std::endl;
	}

	cameras.Initialize(usableDeviceInfos.size());

	// Seed the random number generator and generate a random device key value.
	srand((unsigned) time(nullptr));
	DeviceKey = rand();

	// For the following sample we use the CActionTriggerConfiguration to configure the camera.
	// It will set the DeviceKey, GroupKey and GroupMask features. It will also
	// configure the camera FrameTrigger and set the TriggerSource to the action command.
	// You can look at the implementation of CActionTriggerConfiguration in <pylon/gige/ActionTriggerConfiguration.h>
	// to see which features are set.

	// vector of serial number to sort and order the camera idx
	struct SerialNumIdx {
		String_t number;
		size_t index;
	};
	std::vector<SerialNumIdx> sn{};

	// Create all GigE cameras and attach them to the InstantCameras in the array.
	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		cameras[i].Attach(tlFactory.CreateDevice(usableDeviceInfos[i]));
		// We'll use the CActionTriggerConfiguration, which will set up the cameras to wait for an action command.
		if (useExternalTrigger == false) {
			cameras[i].RegisterConfiguration(new CActionTriggerConfiguration { DeviceKey, GroupKey, AllGroupMask }, RegistrationMode_Append,
					Cleanup_Delete);
		}
		// Set the context. This will help us later to correlate the grab result to a camera in the array.
		cameras[i].SetCameraContext(i);

		const CBaslerGigEDeviceInfo& di = cameras[i].GetDeviceInfo();

		// Print the model name of the camera.
		cout << "Using camera " << i << ": " << di.GetModelName() << " (" << di.GetIpAddress() << ")" << " - (SN:" << di.GetSerialNumber() << ")" << endl;

		SerialNumIdx elem {di.GetSerialNumber(), i};
		// push the serial numbers and the idx positions into vectors
		sn.push_back(elem);
	}

	// Sort the serial numbers in increasing value
	std::sort(sn.begin(), sn.end(), [](const auto &e1, const auto &e2) {
		return e1.number < e2.number;
	});

	// Store the indices of the camera corresponding to the increasing value of serial numbers
	for (const auto &x : sn) {
		sortedCameraIdx.push_back(x.index);
	}

	// Open all cameras.
	// This will apply the CActionTriggerConfiguration specified above.
	cameras.Open();

	// Reads the camera parameters from file
	if (loadParam) {
		try {
			LoadParameters();
		} catch (const GenericException &e) {
			// Error handling
			cerr << "Error loading the parameters of the camera." << std::endl;
			cerr <<  e.GetDescription() << endl;
		}
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		// This sets the transfer pixel format to BayerRG8
		cameras[i].PixelFormat.SetValue(PixelFormat_BayerRG8);

		cameras[i].GevSCPSPacketSize.SetValue(8192);
		//cameras[i].GevSCPSPacketSize.SetValue(9000);
		cameras[i].GevSCPD.SetValue(3500); // Inter-packet delay
		//cameras[i].GevSCPD.SetValue(20); // Inter-packet delay
		//cameras[i].GevSCFTD.SetValue(0); // Frame-transmission delay
        if (i==0)
            cameras[i].GevSCFTD.SetValue(0); // Frame-transmission delay
        else
            cameras[i].GevSCFTD.SetValue(2000); // Frame-transmission delay

		cameras[i].GevSCBWRA.SetValue(cameras[i].GevSCBWRA.GetMax());

		cameras[i].GainAuto.SetValue(GainAuto_Off);
		cameras[i].ExposureAuto.SetValue(ExposureAuto_Off);

		cameras[i].ExposureTimeAbs.SetValue(exposureTime);
		cameras[i].GainRaw.SetValue(gain);

		cameras[i].Width.SetValue(100);
		cameras[i].Height.SetValue(100);
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		if (i == 0) {
			if (IsWritable(cameras[sortedCameraIdx[0]].OffsetX)) {
				cameras[sortedCameraIdx[0]].OffsetX.SetValue(offsetX_0);
			}
			if (IsWritable(cameras[sortedCameraIdx[0]].OffsetY)) {
				cameras[sortedCameraIdx[0]].OffsetY.SetValue(offsetY_0);
			}
		} else if (i == 1) {
			if (IsWritable(cameras[sortedCameraIdx[1]].OffsetX)) {
				cameras[sortedCameraIdx[1]].OffsetX.SetValue(offsetX_1);
			}

			if (IsWritable(cameras[sortedCameraIdx[1]].OffsetY)) {
				cameras[sortedCameraIdx[1]].OffsetY.SetValue(offsetY_1);
			}
		}
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {

		cameras[i].Width.SetValue(width);
		cameras[i].Height.SetValue(height);

		cameras[i].AutoFunctionAOISelector.SetValue(AutoFunctionAOISelector_AOI1);
		cameras[i].AutoFunctionAOIWidth.SetValue(aoi_width);
		cameras[i].AutoFunctionAOIHeight.SetValue(aoi_height);
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		if (i == 0) {
			cameras[sortedCameraIdx[0]].AutoFunctionAOIOffsetX.SetValue(aoi_offsetX_0);
			cameras[sortedCameraIdx[0]].AutoFunctionAOIOffsetY.SetValue(aoi_offsetY_0);
		} else if (i == 1) {
			cameras[sortedCameraIdx[1]].AutoFunctionAOIOffsetX.SetValue(aoi_offsetX_1);
			cameras[sortedCameraIdx[1]].AutoFunctionAOIOffsetY.SetValue(aoi_offsetY_1);
		}
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {

		cameras[i].AutoTargetValue.SetValue(autoTargetVal);

		// Sets auto adjustments continuous
		if (autoExpTimeCont)
			cameras[i].ExposureAuto.SetValue(ExposureAuto_Continuous);
		if (autoGainCont)
			cameras[i].GainAuto.SetValue(GainAuto_Continuous);
	}

	if (useExternalTrigger == true) {
		// Configuration for external trigger
		for (size_t i = 0; i < cameras.GetSize(); ++i) {
			cameras[i].AcquisitionMode.SetValue(AcquisitionMode_Continuous);
			//cameras[i].TriggerSelector.SetValue(TriggerSelector_AcquisitionStart);
			cameras[i].TriggerSelector.SetValue(TriggerSelector_FrameStart);
			cameras[i].TriggerMode.SetValue(TriggerMode_On);
			cameras[i].TriggerSource.SetValue(TriggerSource_Line1);
			cameras[i].TriggerActivation.SetValue(TriggerActivation_RisingEdge);
		}
	}

	if (useChunkFeatures == true) {
		// Configuration for chunk features
		for (size_t i = 0; i < cameras.GetSize(); ++i) {

			// Enable chunks in general.
	        if (GenApi::IsWritable(cameras[i].ChunkModeActive))
	        {
	            cameras[i].ChunkModeActive.SetValue(true);
	        }
	        else
	        {
	            throw RUNTIME_EXCEPTION( "The camera doesn't support chunk features");
	        }

	        // Enable time stamp chunks.
	        cameras[i].ChunkSelector.SetValue(ChunkSelector_Timestamp);
			cameras[i].ChunkEnable.SetValue(true);
			cameras[i].ChunkSelector.SetValue(ChunkSelector_ExposureTime);
			cameras[i].ChunkEnable.SetValue(true);
			cameras[i].ChunkSelector.SetValue(ChunkSelector_GainAll);
			cameras[i].ChunkEnable.SetValue(true);
		}
	}

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		if (i == 0) {
			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
			balanceR_0 = cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue();
			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
			balanceG_0 = cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue();
			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
			balanceB_0 = cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue();
		} else if (i == 1) {
			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
			balanceR_1 = cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue();
			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
			balanceG_1 = cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue();
			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
			balanceB_1 = cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue();
		}
	}


	// Starts grabbing for all cameras.
	// The cameras won't transmit any image data, because they are configured to wait for an action command.

	cameras.StartGrabbing();

}

void Cameras::IssueActionCommand() {
	//////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////
	// Use an Action Command to Trigger Multiple Cameras at the Same Time.
	//////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////
	cout << endl << "Issuing an action command." << endl;

	try {

//		const int DefaultTimeout_ms { 5000 };

		std::string captureTimeCPU = StampTime();

		// Now we issue the action command to all devices in the subnet.
		// The devices with a matching DeviceKey, GroupKey and valid GroupMask will grab an image.
		pTL->IssueActionCommand(DeviceKey, GroupKey, AllGroupMask, subnet);

		// If the action command is successful push the time stamp for retrieving the image
		triggerQueue.push ( captureTimeCPU );

//		cameras[0].WaitForFrameTriggerReady(DefaultTimeout_ms, TimeoutHandling_ThrowException);
//		cameras[1].WaitForFrameTriggerReady(DefaultTimeout_ms, TimeoutHandling_ThrowException);

	} catch (const GenericException &e) {
		// Error handling
		cerr << "=============================================================" << endl;
		cerr << "An exception occurred." << endl << e.GetDescription() << endl;
		cerr << "=============================================================" << endl;
	} catch (const std::exception &e) {
		cerr << "=============================================================" << endl;
		cerr << "An exception occurred." << endl << e.what() << endl;
		cerr << "=============================================================" << endl;
	}

}

void Cameras::GrabImages() {

	std::chrono::high_resolution_clock::time_point t1 { };
	std::chrono::high_resolution_clock::time_point t2 { };

	try {

		std::string captureTimeCPU {};

		if (useExternalTrigger == false) {
			std::shared_ptr<std::string> timeStamp { };
			timeStamp = triggerQueue.wait_pop();
			captureTimeCPU = *timeStamp;
		}

		const int DefaultTimeout_ms { 5000 };

		// This smart pointer will receive the grab result data.
		CBaslerGigEGrabResultPtr ptrGrabResult { };

		// Create an Image objects for the grabbed data
		ImagesRaw img0 {};
		ImagesRaw img1 {};

		if (cameras.GetSize() >= 1) {
			img0.setCameraIdx(0);
			//img0.setCaptureTime(captureTime);
//			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
//			img0.setBalanceR(cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue());
//			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
//			img0.setBalanceG(cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue());
//			cameras[sortedCameraIdx[0]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
//			img0.setBalanceB(cameras[sortedCameraIdx[0]].BalanceRatioAbs.GetValue());
			img0.setAutoExpTime(static_cast<int>(autoExpTimeCont));
			img0.setAutoGain(static_cast<int>(autoGainCont));
			std::stringstream ss1 { };
			std::string tstr1 { };
			ss1 << cameras[sortedCameraIdx[0]].GetDeviceInfo().GetSerialNumber().c_str();
			ss1 >> tstr1;
			img0.setSerialNumber(tstr1);
		}

		if (cameras.GetSize() == 2) {
			img1.setCameraIdx(1);
			//img1.setCaptureTime(captureTime);
//			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
//			img1.setBalanceR(cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue());
//			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
//			img1.setBalanceG(cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue());
//			cameras[sortedCameraIdx[1]].BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
//			img1.setBalanceB(cameras[sortedCameraIdx[1]].BalanceRatioAbs.GetValue());
			img1.setAutoExpTime(static_cast<int>(autoExpTimeCont));
			img1.setAutoGain(static_cast<int>(autoGainCont));
			std::stringstream ss2 { };
			std::string tstr2 { };
			ss2 << cameras[sortedCameraIdx[1]].GetDeviceInfo().GetSerialNumber().c_str();
			ss2 >> tstr2;
			img1.setSerialNumber(tstr2);
		}

		// Retrieve images from all cameras.

		t1 = std::chrono::high_resolution_clock::now();

		for (size_t i = 0; i < cameras.GetSize() && cameras.IsGrabbing(); ++i) {

			std::string captureTimeCam {};
			double exposureTime {};
			int gain {};

			// CInstantCameraArray::RetrieveResult will return grab results in the order they arrive.
			cameras.RetrieveResult(DefaultTimeout_ms, ptrGrabResult, TimeoutHandling_ThrowException);

			// When the cameras in the array are created the camera context value
			// is set to the index of the camera in the array.
			// The camera context is a user-settable value.
			// This value is attached to each grab result and can be used
			// to determine the camera that produced the grab result.
			intptr_t cameraIndex = ptrGrabResult->GetCameraContext();

			// Image grabbed successfully?
			if (ptrGrabResult->GrabSucceeded()) {
				// Print the index and the model name of the camera.
				cout << "Camera " << sortedCameraIdx[cameraIndex] << ": " << cameras[cameraIndex].GetDeviceInfo().GetModelName() << " ("
						<< cameras[cameraIndex].GetDeviceInfo().GetIpAddress() << ") (SN:" << cameras[cameraIndex].GetDeviceInfo().GetSerialNumber()
						<< ")" << endl;
				// You could process the image here by accessing the image buffer.
				cout << "GrabSucceeded: " << ptrGrabResult->GrabSucceeded() << endl;
				uint8_t *pImageBuffer = static_cast<uint8_t *>(ptrGrabResult->GetBuffer());

				if (useExternalTrigger == true) {
					captureTimeCPU = StampTime();
				}

				if (useChunkFeatures == true) {
					// Check to see if a buffer containing chunk data has been received.
					if (PayloadType_ChunkData != ptrGrabResult->GetPayloadType()) {
						throw RUNTIME_EXCEPTION( "Unexpected payload type received.");
					}

		            // Access the chunk data attached to the result.
		            // Before accessing the chunk data, you should check to see
		            // if the chunk is readable. When it is readable, the buffer
		            // contains the requested chunk data.
		            if (IsReadable(ptrGrabResult->ChunkTimestamp)) {
		            	std::ostringstream oss{};
		            	oss << ptrGrabResult->ChunkTimestamp.GetValue();
		            	captureTimeCam = oss.str();
		                cout << "TimeStamp (Result): " << captureTimeCam << endl;
		            }
					if (IsReadable(ptrGrabResult->ChunkExposureTime)) {
						exposureTime = ptrGrabResult->ChunkExposureTime.GetValue();
						cout << "ExposureTime (Result): " << exposureTime << endl;
					}
					if (IsReadable(ptrGrabResult->ChunkGainAll)) {
						gain = ptrGrabResult->ChunkGainAll.GetValue();
						cout << "Gain (Result): " << gain << endl;
					}
				}

				// Copy image to the object's buffer
				if (sortedCameraIdx[cameraIndex] == 0) {
					img0.copyBuffer(reinterpret_cast<char *>(pImageBuffer));
					img0.setCaptureCPUTime(captureTimeCPU);
					img0.setCaptureCamTime(captureTimeCam);
					img0.setExposureTime(exposureTime);
					img0.setGain(gain);
					img0.setBalanceR(balanceR_0);
					img0.setBalanceG(balanceG_0);
					img0.setBalanceB(balanceB_0);
				} else {
					img1.copyBuffer(reinterpret_cast<char *>(pImageBuffer));
					img1.setCaptureCPUTime(captureTimeCPU);
					img1.setCaptureCamTime(captureTimeCam);
					img1.setExposureTime(exposureTime);
					img1.setGain(gain);
					img1.setBalanceR(balanceR_1);
					img1.setBalanceG(balanceG_1);
					img1.setBalanceB(balanceB_1);
				}

				cout << "Gray value of first pixel: " << static_cast<uint32_t>(pImageBuffer[0]) << endl << endl;
			} else {
				// If a buffer has been incompletely grabbed, the network bandwidth is possibly insufficient for transferring
				// multiple images simultaneously. See note above c_maxCamerasToUse.
				cout << "Error: " << ptrGrabResult->GetErrorCode() << " " << ptrGrabResult->GetErrorDescription() << endl;
				throw std::runtime_error ("Buffer was incompletely grabbed.");
			}
		}

		t2 = std::chrono::high_resolution_clock::now();

		total_duration_grab_int += t2 - t1;
		number_grab_int++;

		PairImages imgs2store { std::move(img0), std::move(img1) };
		imgDisplayQueue.push(imgs2store);


		// In case you want to trigger again you should wait for the camera
		// to become trigger-ready before issuing the next action command.
		// To avoid overtriggering you should call cameras[0].WaitForFrameTriggerReady
		// (see Grab_UsingGrabLoopThread sample for details).

	} catch (const GenericException &e) {
		// Error handling
		cerr << "=============================================================" << endl;
		cerr << "An exception occurred." << endl << e.GetDescription() << endl;
		cerr << "=============================================================" << endl;
	} catch (const std::exception &e) {
		cerr << "=============================================================" << endl;
		cerr << "An exception occurred." << endl << e.what() << endl;
		cerr << "=============================================================" << endl;
	}

}




#include "EquiToPinhole.hpp"



template<typename T>
inline void polar2Spherical (const T &theta, const T &phi, cv::Matx<T,1,3> &p) {
// Converts polar coordinates to spherical coordinates
// Inputs:
//		theta: scans on the x-y direction and is in the range [0,2pi]
//		phi: scans on the z direction and is in the range [-pi/2, pi/2]
// Output:
//		p: the point in spherical coordinates

	// coordinate conversion
	p(0) = static_cast<T>(cos(theta) * cos(phi));
	p(1) = static_cast<T>(sin(theta) * cos(phi));
	p(2) = static_cast<T>(sin(phi));
}

template<typename T>
inline void Spherical2Polar (const cv::Matx<T,1,3> &v, T &theta, T &phi){
// Converts from spherical coordinates to polar coordinates
// Input:
//	v	: spherical coordinates
// Outputs:
//	theta	: the angle that corresponds to the x-direction on an equirectangular image, it goes from 0 (left) to 2pi (right)
// 	rho		: the angle that corresponds to the y-direction on an equirectangular image, it goes from pi/2 (top) to -pi/2 (bottom)
//
// Remarks:
//
//		    y
//         pi/2
//	    	|
//		    |
//   pi ----------- 0 x
//   		|
// 	    	|
//     	  3/2*pi
//
//			z
//		   pi/2
// 			|
//          |
//  -----------------
//			|
//			|
//		  -pi/2

	const T &x = v(0);
	const T &y = v(1);
	const T &z = v(2);

	phi = asin(z);

	T h = sqrt(x * x + y * y);

	if (y >= 0) {
		theta = acos(x / h);
	} else {
		theta = 2 * M_PI - acos(x / h);
	}

}

//void mapToVec2f(cv:Vec2s &m1, short &m2, float &u, float &v){
//    m1[j*2] = (short)(iu >> cv::INTER_BITS);
//    m1[j*2+1] = (short)(iv >> cv::INTER_BITS);
//    m2[j] = (ushort)((iv & (cv::INTER_TAB_SIZE-1))*cv::INTER_TAB_SIZE + (iu & (cv::INTER_TAB_SIZE-1)));
//}
//
//void vec2fToMap(float u, float v, cv::Vec2s &m1, short &m2){
//    int iu = cv::saturate_cast<int>(u*cv::INTER_TAB_SIZE);
//    int iv = cv::saturate_cast<int>(v*cv::INTER_TAB_SIZE);
//    m1[0] = (short)(iu >> cv::INTER_BITS);
//    m1[1] = (short)(iv >> cv::INTER_BITS);
//    m2 = (ushort)((iv & (cv::INTER_TAB_SIZE-1))*cv::INTER_TAB_SIZE + (iu & (cv::INTER_TAB_SIZE-1)));
//}


void rotateMap(cv::Mat &mapX, cv::Mat &mapY, cv::Mat &mapXRot, cv::Mat &mapYRot, float alpha){
//	alpha = 0;
	int w = mapX.cols, h = mapX.rows;
	for(int y = 0; y < mapX.rows;y++){
		for(int x = 0; x < mapX.cols;x++){
			float theta = M_PI*x/w-M_PI/2;
			float phi   = M_PI/2-M_PI*y/h;
			cv::Matx13f s;
			polar2Spherical<float>(theta, phi, s);
			cv::Matx13f sRot;
			sRot(1) = s(1) * cos(alpha) - s(2) * sin(alpha);
			sRot(2) = s(1) * sin(alpha) + s(2) * cos(alpha);
			sRot(0) = s(0);
			float thetaRot;
			float phiRot;
			Spherical2Polar<float>(sRot, thetaRot, phiRot);
			float xRot = (thetaRot+M_PI/2)*w/M_PI;
			float yRot = (M_PI/2-phiRot)*h/M_PI;
			if(x == 1000 && y == 3008/2){
				int xs = 2;
			}
			int xi = (int)xRot %(w), yi = (int)yRot %(h);
			if(xi > 0 && xi < w-1 && yi > 0 && yi < h-1){
				float xfb = xRot-((int)xRot), yfb = yRot-((int)yRot);
				float xfa = 1.0f-xfb, yfa = 1.0f-yfb;
				mapXRot.at<float>(y, x) = (mapX.at<float>(yi, xi)*xfa + mapX.at<float>(yi, xi+1)*xfb)*yfa + (mapX.at<float>(yi+1, xi)*xfa + mapX.at<float>(yi+1, xi+1)*xfb)*yfb;
				mapYRot.at<float>(y, x) = (mapY.at<float>(yi, xi)*xfa + mapY.at<float>(yi, xi+1)*xfb)*yfa + (mapY.at<float>(yi+1, xi)*xfa + mapY.at<float>(yi+1, xi+1)*xfb)*yfb;
			} else {
				mapXRot.at<float>(y, x) = 0;
				mapYRot.at<float>(y, x) = 0;
			}
//			mapXRot.at<float>(y, x) = mapX.at<float>(yi, xi);
//			mapYRot.at<float>(y, x) = mapY.at<float>(yi, xi);


		}
	}
}

string type2str(int type) {
  string r;

  uchar depth = type & CV_MAT_DEPTH_MASK;
  uchar chans = 1 + (type >> CV_CN_SHIFT);

  switch ( depth ) {
    case CV_8U:  r = "8U"; break;
    case CV_8S:  r = "8S"; break;
    case CV_16U: r = "16U"; break;
    case CV_16S: r = "16S"; break;
    case CV_32S: r = "32S"; break;
    case CV_32F: r = "32F"; break;
    case CV_64F: r = "64F"; break;
    default:     r = "User"; break;
  }

  r += "C";
  r += (chans+'0');

  return r;
}



#include "opencv2/highgui/highgui.hpp"
void rotCalibClick(int event, int x, int y, int flags, void* userdata)
{
	auto ctx = (RotCalibContext*)userdata;
	if  ( event == cv::EVENT_LBUTTONDOWN ){
		ctx->pos[ctx->clickCounter].x = x;
		ctx->pos[ctx->clickCounter].y = y;
		ctx->clickCounter += 1;
		ctx->clickCounter &= 1;
	}
}




void Cameras::DisplayImages() {
	int key { };
	std::shared_ptr<PairImages> imgs { };
	imgs = imgDisplayQueue.wait_pop();

	std::chrono::high_resolution_clock::time_point t1{};
	std::chrono::high_resolution_clock::time_point t2{};

	t1 = std::chrono::high_resolution_clock::now();
	PairImages imgs2 {*imgs};
	imgs2.convertRaw2CV();
	t2 = std::chrono::high_resolution_clock::now();

	total_duration_raw2cv += t2 - t1;
	number_conversions_raw2cv++;

	std::chrono::high_resolution_clock::time_point t3 { };
	std::chrono::high_resolution_clock::time_point t4 { };

	t3 = std::chrono::high_resolution_clock::now();
	PairImages imgs3 {imgs2};


	imgs3.convertCV2Equi(map_0_1s, map_0_2s, map_1_1s, map_1_2s);
	imgs3.showPairConcat();


	t4 = std::chrono::high_resolution_clock::now();

	total_duration_cv2equi += t4 - t3;
	number_conversions_cv2equi++;




	if(pineholeDisplayEnable){
		cv::Mat concat = imgs3.rgbConcat();
		cv::Mat pinhole1(1000, 1000, CV_8UC3), pinhole2(1000, 1000, CV_8UC3);
		equiToPinhole(concat, pinhole1, 60, 0, 0);
		equiToPinhole(concat, pinhole2, 60, M_PI, 0);

		rotCalibContexts[0].draw(pinhole1);
		rotCalibContexts[1].draw(pinhole2);

		cv::namedWindow("pinhole1", cv::WINDOW_NORMAL);
		cv::setMouseCallback("pinhole1", rotCalibClick, &rotCalibContexts[0]);
		imshow( "pinhole1", pinhole1 );
		cv::namedWindow("pinhole2", cv::WINDOW_NORMAL);
		cv::setMouseCallback("pinhole2", rotCalibClick, &rotCalibContexts[1]);
		imshow( "pinhole2", pinhole2 );
	}

	key = cv::waitKey(20);


	if(key == 'p'){
		pineholeDisplayEnable = !pineholeDisplayEnable;
	}

	bool genMap = false;
	if(key == 'f'){
		imgDisplayQueue.flush();
	}
	if(key == '2'){
		rotCalibAlpha -= M_PI/18000;
		genMap = true;
	}
	if(key == '3'){
		rotCalibAlpha += M_PI/18000;
		genMap = true;
	}
	if(key == '1'){
		rotCalibAlpha -= M_PI/1800;
		genMap = true;
	}
	if(key == '4'){
		rotCalibAlpha += M_PI/1800;
		genMap = true;
	}
//	genMap = true;
//
//	rotCalibAlpha += M_PI/180;
	if(genMap){
		cv::Mat map_1_1_rotf(map_1_1f.rows, map_1_1f.cols, CV_32FC1);
		cv::Mat map_1_2_rotf(map_1_2f.rows, map_1_2f.cols, CV_32FC1);

		rotateMap(map_1_1f, map_1_2f, map_1_1_rotf, map_1_2_rotf, rotCalibAlpha);
		cv::convertMaps(map_1_1_rotf, map_1_2_rotf, map_1_1s, map_1_2s, CV_16SC2);
		imgDisplayQueue.flush();
		cout << "************** " << rotCalibAlpha << endl;
		cout << "************** " << rotCalibAlpha << endl;
		cout << "************** " << rotCalibAlpha << endl;
		cout << "************** " << rotCalibAlpha << endl;
		cout << "************** " << rotCalibAlpha << endl;
		cv::waitKey(200);
	}

	if(key == '5'){
		cv::Mat map_1_1_rotf(map_1_1f.rows, map_1_1f.cols, CV_32FC1);
		cv::Mat map_1_2_rotf(map_1_2f.rows, map_1_2f.cols, CV_32FC1);
		rotateMap(map_1_1f, map_1_2f, map_1_1_rotf, map_1_2_rotf, rotCalibAlpha);
		cv::FileStorage map1File("map1.xml", cv::FileStorage::WRITE);
		map1File << "mat_map1" << map_1_1_rotf;
		cv::FileStorage map2File("map2.xml", cv::FileStorage::WRITE);
		map2File << "mat_map2" << map_1_2_rotf;
	}
	if (key == 27) {
		// if ESC key is pressed signal to exit the program
		exitProgram = true;
		imgStorageQueue.push (*imgs);
	} else if ((key == 's') || (key == 'S') || startSaving) {
		++imgNum; // increase the image number;
		imgs->setImgNumber(imgNum);
		imgStorageQueue.push (*imgs);
//		imgs2.setImgNumber(imgNum);
//		imgStorageQueue.push (imgs2);
//		imgs3.setImgNumber(imgNum);
//		imgStorageQueue.push (imgs3);
		startSaving = true;
	} else if ((key == 'c') || (key == 'C') || startSaving) {
		++imgNum; // increase the image number;
		imgs->setImgNumber(imgNum);
		imgStorageQueue.push (*imgs);
//		imgs2.setImgNumber(imgNum);
//		imgStorageQueue.push (imgs2);
//		imgs3.setImgNumber(imgNum);
//		imgStorageQueue.push (imgs3);
		startSaving = false;
	}
}

void Cameras::DemoLoadImages() {
	int key { };

	ImagesRaw img0 { data_path + "1_0.raw" };
	ImagesRaw img1 { data_path + "1_1.raw" };

	PairImages imgs { img0, img1 };

//	std::shared_ptr<PairImages> imgs { };
//	imgs = imgDisplayQueue.wait_pop();
	imgs.showPair();
	/*imgs.showPairConcat();
	imgs.showUndistortPairConcat(map_0_1, map_0_2, map_1_1, map_1_2);*/
	key = cv::waitKey(1);

	if (key == 27) {
		// if ESC key is pressed signal to exit the program
		exitProgram = true;
		//imgStorageQueue.push (*imgs);
	} /*else if ((key == 83) || (key == 115) || startSaving) {
		++imgNum; // increase the image number;
		imgs->setImgNumber(imgNum);
		imgStorageQueue.push (*imgs);
		startSaving = true;
	}*/



}

void Cameras::StoreImages() {

	std::chrono::high_resolution_clock::time_point t1 { };
	std::chrono::high_resolution_clock::time_point t2 { };

	std::shared_ptr<PairImages> imgs { };
	imgs = imgStorageQueue.wait_pop();
	if (exitProgram != true) {
		t1 = std::chrono::high_resolution_clock::now();
		imgs->savePair(data_path);
		t2 = std::chrono::high_resolution_clock::now();

		if (imgs->getType() == ImgType::RAW) {
			total_duration_sto_raw += t2 - t1;
			number_sto_raw++;
		} else if (imgs->getType() == ImgType::CV) {
			total_duration_sto_cv += t2 - t1;
			number_sto_cv++;
		} else if (imgs->getType() == ImgType::EQUI) {
			total_duration_sto_equi += t2 - t1;
			number_sto_equi++;

		}
	}

}

void Cameras::SaveParameters(){

	std::vector<String_t> sn{};

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		sn.push_back(cameras[i].GetDeviceInfo().GetSerialNumber());
		std::stringstream ss{};
		ss << sn[i].c_str();
		ss << ".pfs";
		std::string filename = config_path + "/" + ss.str();

		//const char Filename[name.size()+1] { name.c_str() };
		cout << "Saving camera's node map to file..." << endl;
		// Save the content of the camera's node map into the file.
		CFeaturePersistence::Save(filename.c_str(), &cameras[i].GetNodeMap());
	}

}

void Cameras::LoadParameters(){

	std::vector<String_t> sn{};

	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		sn.push_back(cameras[i].GetDeviceInfo().GetSerialNumber());
		std::stringstream ss { };
		ss << sn[i].c_str();
		ss << ".pfs";
		std::string filename = config_path + "/" + ss.str();

	    std::cout << "Reading file back to camera's node map for camera with SN:"<< cameras[i].GetDeviceInfo().GetSerialNumber() << " ..." << std::endl;
	    CFeaturePersistence::Load(filename.c_str(), &cameras[i].GetNodeMap(), true );
	}
}

void Cameras::LoadCameraConfig() {

	std::string path_data = config_path + "genparam.cfg";
	std::ifstream myFile(path_data);
	if (myFile.is_open()) {
		std::stringstream ss { };
		std::string line { };

		getline(myFile, line);
		std::string token = line.substr(line.find_last_of(":") + 1);
		ss << token;
		ss >> data_path;
		std::cout << "Data path: " << data_path << std::endl;

		getline(myFile, line);
		token = line.substr(line.find_last_of(":") + 1);
		ss.str(std::string());
		ss.clear();
		ss << token;
		int val { };
		ss >> val;
		autoExpTimeCont = static_cast<bool>(val);
		std::cout << "Auto Exposure Time Continuous: " << autoExpTimeCont << std::endl;

		getline(myFile, line);
		token = line.substr(line.find_last_of(":") + 1);
		ss.str(std::string());
		ss.clear();
		ss << token;
		val = 0;
		ss >> val;
		autoGainCont = static_cast<bool>(val);
		std::cout << "Auto Gain Continuous: " << autoGainCont << std::endl;

		getline(myFile, line);
		token = line.substr(line.find_last_of(":") + 1);
		ss.str(std::string());
		ss.clear();
		ss << token;
		ss >> exposureTime;
		std::cout << "Exposure Time: " << exposureTime << std::endl;

		getline(myFile, line);
		token = line.substr(line.find_last_of(":") + 1);
		ss.str(std::string());
		ss.clear();
		ss << token;
		ss >> gain;
		std::cout << "Gain: " << gain << std::endl;

		getline(myFile, line);
		token = line.substr(line.find_last_of(":") + 1);
		ss.str(std::string());
		ss.clear();
		ss << token;
		ss >> path_cal;
		std::cout << "Path to calibration directory: " << path_cal << std::endl;

		myFile.close();

	} else {
		throw std::runtime_error("Could not open the file to load camera data");
	}

}

void Cameras::LoadMap() {


	if (cameras.GetSize() >= 1) {
		String_t sn1 { };
		sn1 = cameras[sortedCameraIdx[0]].GetDeviceInfo().GetSerialNumber();

		std::stringstream ss1 { };
		ss1 << path_cal;
		ss1 << "calibration_";
		ss1 << sn1.c_str();
		ss1 << "/map1.xml";
		std::string filename1 = ss1.str();

		cv::FileStorage file_0_1(filename1, cv::FileStorage::READ);
		if (file_0_1.isOpened()) {
			file_0_1["mat_map1"] >> map_0_1f;
			file_0_1.release();
			std::cout << "Read " << filename1 << std::endl;
		} else {
			throw std::runtime_error("Could not load map_0_1.");
		}

		std::stringstream ss2 { };
		ss2 << path_cal;
		ss2 << "calibration_";
		ss2 << sn1.c_str();
		ss2 << "/map2.xml";
		std::string filename2 = ss2.str();

		cv::FileStorage file_0_2(filename2, cv::FileStorage::READ);
		if (file_0_2.isOpened()) {
			file_0_2["mat_map2"] >> map_0_2f;
			file_0_2.release();
			std::cout << "Read " << filename2 << std::endl;
		} else {
			throw std::runtime_error("Could not load map_0_2.");
		}

		cv::convertMaps(map_0_1f, map_0_2f, map_0_1s, map_0_2s, CV_16SC2);
		if (cameras.GetSize() == 1) {
			map_1_1f = map_0_1f;
			map_1_2f = map_0_2f;
			map_1_1s = map_0_1s;
			map_1_2s = map_0_2s;
		}
	}

	if (cameras.GetSize() == 2) {

		String_t sn2 { };
		sn2 = cameras[sortedCameraIdx[1]].GetDeviceInfo().GetSerialNumber();

		std::stringstream ss3 { };
		ss3 << path_cal;
		ss3 << "calibration_";
		ss3 << sn2.c_str();
		ss3 << "/map1.xml";
		std::string filename3 = ss3.str();

		cv::FileStorage file_1_1(filename3, cv::FileStorage::READ);
		if (file_1_1.isOpened()) {
			file_1_1["mat_map1"] >> map_1_1f;
			file_1_1.release();
			std::cout << "Read " << filename3 << std::endl;
		} else {
			throw std::runtime_error("Could not load map_1_1.");
		}

		std::stringstream ss4 { };
		ss4 << path_cal;
		ss4 << "calibration_";
		ss4 << sn2.c_str();
		ss4 << "/map2.xml";
		std::string filename4 = ss4.str();

		cv::FileStorage file_1_2(filename4, cv::FileStorage::READ);
		if (file_1_2.isOpened()) {
			file_1_2["mat_map2"] >> map_1_2f;
			file_1_2.release();
			std::cout << "Read " << filename4 << std::endl;
		} else {
			throw std::runtime_error("Could not load map_0_2.");
		}

		cv::convertMaps(map_1_1f, map_1_2f, map_1_1s, map_1_2s, CV_16SC2);
	}

}

size_t Cameras::GetNumCam() const {
	return cameras.GetSize();
}

inline std::string Cameras::StampTime() {
	timeval curTime{};
	gettimeofday(&curTime, nullptr);
	long int milli { curTime.tv_usec / 1000 };
	long int micro { curTime.tv_usec % 1000 };

	char buffer [80] {};
	strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", localtime(&curTime.tv_sec));

	char currentTime[84] = "";
	sprintf(currentTime, "%s:%d:%d", buffer, static_cast<int> (milli), static_cast<int> (micro));
	std::string myTime {currentTime};
	return myTime;
}

Cameras::~Cameras() {

	cameras.StopGrabbing();


	for (size_t i = 0; i < cameras.GetSize(); ++i) {
		cameras[i].DeviceReset();
	}


	// Close all cameras.
	cameras.Close();
}

} /* namespace ScanVan */
