#include <stdio.h>
#include <opencv2/opencv.hpp>
#include "CameraDS.h"

int main()
{
	int camCount = CCameraDS::CameraCount();
	CCameraDS camDS;
	for (int i = 0; i < camCount; i++) {
		char szCamName[1024];
		int retval = camDS.CameraName(i, szCamName, sizeof(szCamName));
		if (retval > 0)
			printf("Camera #%d: %s\n", i, szCamName);
		else
			printf("Camera #%d: no name\n", i);
	}

	const int camID = 2;
	if (!camDS.OpenCamera(camID, true))
		printf("Camera #%d cannot be opened.\n", camID);

	for (;;) {
		IplImage *pFrame = camDS.QueryFrame();
		if (pFrame == NULL) {
			printf("Cannot grab frames from Camera #%d.\n", camID);
			break;
		}
		cv::Mat frame = cv::cvarrToMat(pFrame);
		cv::resize(frame, frame, cv::Size(1280, 800), 0, 0, cv::INTER_CUBIC);
		cv::imshow("input", frame);
		if (cv::waitKey(33) == 27)
			break;
	}
	cv::destroyAllWindows();
	
	return 0;
}