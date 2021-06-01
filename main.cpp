#include "MJPEGWriter.h"

using namespace std;
using namespace cv;

int main()
{
    MJPEG server(7777);
    VideoCapture cap;
    bool ok = cap.open(0);
    if (!ok)
    {
        cerr << "No cam found" << endl;
        return 1;
    }
    //cap.set(CV_CAP_PROP_FRAME_WIDTH, 1296);
    //cap.set(CV_CAP_PROP_FRAME_HEIGHT, 972);
    Mat frame;
    cap >> frame;
    server.write(frame);
    frame.release();
    server.start();
    while (cap.isOpened())
    {
        cap >> frame;
        server.write(frame);
        frame.release();
        mySleep(40);
    }
    cout << "Camera shutdown" << endl;
    server.stop();
    return 0;
}