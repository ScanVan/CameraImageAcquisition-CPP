//============================================================================
// Name        : Core.cpp
// Author      : Marcelo Kaihara
// Version     : 1.0
// Copyright   : 
// Description : Main source code. It grabs images from the two cameras and
//				 displays them on the screen and stores them in the disk
//============================================================================

#include <time.h>   // for time
#include <stdlib.h> // for rand & srand

#include <iostream>
#include <unistd.h>
#include <chrono>
#include <thread>

// Include files to use OpenCV API
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

// Include files to use the PYLON API.
#include <pylon/PylonIncludes.h>
#include <pylon/gige/PylonGigEIncludes.h>
#include <pylon/gige/ActionTriggerConfiguration.h>
#include "Cameras.hpp"

#include <time.h>
#include <chrono>
#include "ImagesRaw.hpp"

// Settings to use Basler GigE cameras.
using namespace Basler_GigECameraParams;

// Namespace for using pylon objects.
using namespace Pylon;

// Namespace for using cout.
using namespace std;

// Namespace for Cameras
using namespace ScanVan;

std::string GetCurrentWorkingDir( void ) {
// gets the current working directory
	std::array<char, FILENAME_MAX> buff { { } };
	if (getcwd(buff.data(), FILENAME_MAX) == nullptr) {
		throw(std::runtime_error("The directory could not be determined."));
	}
	std::string current_working_dir(buff.data());
	return current_working_dir;
}


void IssueTrigger (ScanVan::Cameras *cams) {

	//std::chrono::system_clock::time_point PreviousStartTime { std::chrono::system_clock::now() };
	std::chrono::system_clock::time_point StartTime { std::chrono::system_clock::now() };
	const std::chrono::microseconds intervalPeriodMicros { static_cast<int>(1 / (cams->getFps()) * 1000000) };

	std::chrono::system_clock::time_point nextStartTime { };

	long int counter { 0 };

	// For measuring the trigger time
	std::chrono::high_resolution_clock::time_point t1 { };
	std::chrono::high_resolution_clock::time_point t2 { };

	t1 = std::chrono::high_resolution_clock::now();

	while (cams->getExitStatus() == false) {
		// Setup timer
		//nextStartTime = PreviousStartTime + intervalPeriodMicros;
		StartTime = StartTime + intervalPeriodMicros;
		//std::this_thread::sleep_until(nextStartTime);
		std::this_thread::sleep_until(StartTime);

		//PreviousStartTime = std::chrono::system_clock::now();

		cams->IssueActionCommand();

		++counter;
	}

	t2 = std::chrono::high_resolution_clock::now();

	// Measure duration of grabbing
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
	cout << "===>Time lapse issue trigger: " << duration / counter / double(1000) << " ms" << endl;

}

void GrabImages(ScanVan::Cameras *cams) {

	// For measuring the grabbing time
	std::chrono::high_resolution_clock::time_point t1{};
	std::chrono::high_resolution_clock::time_point t2{};
	std::chrono::high_resolution_clock::time_point t1_i{};
	std::chrono::high_resolution_clock::time_point t2_i{};

	// Measure the starting of grabbing
	t1 = std::chrono::high_resolution_clock::now();

	while (cams->getExitStatus() == false) {

		t1_i = std::chrono::high_resolution_clock::now();

		// Grab images
		cams->GrabImages();

		t2_i = std::chrono::high_resolution_clock::now();

		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2_i - t1_i).count();
		cout << "fps: " << double(1000000) / duration << endl;

		std::cout << "DQueue: " << cams->getDisplayQueueSize() << std::endl;
		std::cout << "SQueue: " << cams->getStorageQueueSize() << std::endl;

		cams->inc_grab_counter();

	}

	// Measure the end of the grabbing
	t2 = std::chrono::high_resolution_clock::now();

	// Measure duration of grabbing
	cams->set_grab_duration(std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count());



}

void StoreImages(ScanVan::Cameras *cams) {

	std::chrono::high_resolution_clock::time_point t1 { };
	std::chrono::high_resolution_clock::time_point t2 { };

	// Measure the starting of displaying
	t1 = std::chrono::high_resolution_clock::now();


	while (cams->getExitStatus() == false) {
		cams->StoreImages();
		cams->inc_sto_counter();
	}
	while (cams->imgStorageQueueEmpty() == false) {
		cams->StoreImages();
		cams->inc_sto_counter();
	}

	t2 = std::chrono::high_resolution_clock::now();


	// Measure duration of grabbing
	cams->set_sto_duration(std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count());

}


void DisplayImages(ScanVan::Cameras *cams) {

	std::chrono::high_resolution_clock::time_point t1{};
	std::chrono::high_resolution_clock::time_point t2{};

	// Measure the starting of displaying
	t1 = std::chrono::high_resolution_clock::now();

	while (cams->getExitStatus() == false) {

		cams->DisplayImages();

		cams->inc_disp_counter();

	}
	while (cams->imgDisplayQueueEmpty() == false) {
		cams->DisplayImages();

		cams->inc_disp_counter();
	}

	t2 = std::chrono::high_resolution_clock::now();


	// Measure duration of grabbing
	cams->set_disp_duration(std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count());


}

void DemoLoadImages(ScanVan::Cameras *cams) {

	while (cams->getExitStatus() == false) {

		cams->DemoLoadImages();

	}

}

int main(int argc, char* argv[])
{

    int exitCode { 0 };

    PylonAutoInitTerm autoinitTerm{};

    std::string curr_path = GetCurrentWorkingDir();
    std::string config_path = curr_path + "/" + "config/";
    //std::string data_path = "/media/Windows/Users/marcelo.kaihara.HEVS/Documents/img/";

    try {

    	ScanVan::Cameras cams { config_path };
//		ScanVan::Cameras cams {};
		//cams.setDataPath(data_path);

		//DemoLoadImages(&cams);

		if (cams.getUseExternalTrigger() == false) {
			std::thread thIssueActionCommand(IssueTrigger, &cams);
			std::thread thGrabImages(GrabImages, &cams);
			std::thread thStoreImages(StoreImages, &cams);
			DisplayImages(&cams);

			thIssueActionCommand.join();
			thGrabImages.join();
			thStoreImages.join();

		} else {
			std::thread thGrabImages(GrabImages, &cams);
			std::thread thStoreImages(StoreImages, &cams);
			DisplayImages(&cams);

			thGrabImages.join();
			thStoreImages.join();
		}
		cv::destroyAllWindows();
		//cams.SaveParameters();


		cout << "===>Time lapse grab images: " <<  cams.get_avg_grab() << " ms" << endl;
		cout << "===>Time lapse grab images internal: " <<  cams.get_avg_grab_int() << " ms" << endl;

		cout << "===>Time lapse display images: " << cams.get_avg_disp() << " ms" << endl;
		cout << "===>Time lapse raw to cv : " << cams.get_avg_raw2cv() << " ms" << endl;
		cout << "===>Time lapse cv to equi: " << cams.get_avg_cv2equi() << " ms" << endl;

		cout << "===>Time lapse store images: " << cams.get_avg_sto() << " ms" << endl;
		cout << "===>Time lapse sto raw: " <<  cams.get_avg_sto_raw() << " ms" << endl;
		cout << "===>Time lapse sto cv: " <<  cams.get_avg_sto_cv() << " ms" << endl;
		cout << "===>Time lapse sto equi: " <<  cams.get_avg_sto_equi() << " ms" << endl;

	} catch (const GenericException &e) {
		// Error handling
		cerr << "An exception occurred." << endl << e.GetDescription() << endl;
		exitCode = 1;
	} catch (const std::exception &e) {
		cerr << "An exception ocurred." << endl << e.what() << endl;
		exitCode = 1;

	}



    return exitCode;
}

