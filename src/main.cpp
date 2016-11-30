#include <stdio.h>
#include "camera.hpp"

int main()
{
	Camera camera;
	for (int i = 0; i < Camera::CameraCount(); i++) {
		char szCamName[1024];
		int retval = camera.CameraName(i, szCamName, sizeof(szCamName));
		if (retval > 0)
			printf("Camera #%d: %s\n", i, szCamName);
		else
			printf("Camera #%d: no name\n", i);
	}

	const int camID = 2;
	if (!camera.OpenCamera(camID, true))
		printf("Camera #%d cannot be opened.\n", camID);

	for (;;) {
		cv::Mat frame = camera.QueryFrame();
		cv::resize(frame, frame, cv::Size(1280, 800), 0, 0, cv::INTER_CUBIC);		
		cv::imshow("input", frame);
		if (cv::waitKey(33) == 27)
			break;
	}
	cv::destroyAllWindows();
	
	return 0;
}