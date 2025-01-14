/**
* This file is part of ORB-SLAM3
*
* Copyright (C) 2017-2020 Carlos Campos, Richard Elvira, Juan J. Gómez Rodríguez, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
* Copyright (C) 2014-2016 Raúl Mur-Artal, José M.M. Montiel and Juan D. Tardós, University of Zaragoza.
*
* ORB-SLAM3 is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
* the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with ORB-SLAM3.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include "Frame.h"

#include "G2oTypes.h"
#include "MapPoint.h"
#include "KeyFrame.h"
#include "ORBextractor.h"
#include "Converter.h"
#include "ORBmatcher.h"
#include "GeometricCamera.h"
#include "Object.h"

#include <thread>
#include <include/CameraModels/Pinhole.h>
#include <include/CameraModels/KannalaBrandt8.h>
#include <iomanip>
#include <iterator>
#include <include/Tracking.h>

// The previous image
cv::Mat imGrayPre;
std::vector<cv::Point2f> prepoint, nextpoint;
std::vector<cv::Point2f> F_prepoint, F_nextpoint;
std::vector<cv::Point2f> F2_prepoint, F2_nextpoint;

std::vector<uchar> state;
std::vector<float> err;
std::vector<std::vector<cv::KeyPoint>> mvKeysPre;

namespace ORB_SLAM3
{
//下一个生成的帧的ID,这里是初始化类的静态成员变量
long unsigned int Frame::nNextId=0;

//是否要进行初始化操作的标志
//这里给这个标志置位的操作是在最初系统开始加载到内存的时候进行的，下一帧就是整个系统的第一帧，所以这个标志要置位
bool Frame::mbInitialComputations=true;
float Frame::cx, Frame::cy, Frame::fx, Frame::fy, Frame::invfx, Frame::invfy;
float Frame::mnMinX, Frame::mnMinY, Frame::mnMaxX, Frame::mnMaxY;
float Frame::mfGridElementWidthInv, Frame::mfGridElementHeightInv;
int Frame::imgH, Frame::imgW;

//For stereo fisheye matching
cv::BFMatcher Frame::BFmatcher = cv::BFMatcher(cv::NORM_HAMMING);

Frame::Frame(): mpcpi(NULL), mpImuPreintegrated(NULL), mpPrevFrame(NULL), mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false)
{
#ifdef REGISTER_TIMES
    mTimeStereoMatch = 0;
    mTimeORB_Ext = 0;
#endif
}


//Copy Constructor
Frame::Frame(const Frame &frame)
    :mpcpi(frame.mpcpi),mpORBvocabulary(frame.mpORBvocabulary), mpORBextractorLeft(frame.mpORBextractorLeft), mpORBextractorRight(frame.mpORBextractorRight),
     mTimeStamp(frame.mTimeStamp), mK(frame.mK.clone()), mDistCoef(frame.mDistCoef.clone()),
     mbf(frame.mbf), mb(frame.mb), mThDepth(frame.mThDepth), N(frame.N), mvKeys(frame.mvKeys),
     mvKeysRight(frame.mvKeysRight), mvKeysUn(frame.mvKeysUn), mvuRight(frame.mvuRight),
     mvDepth(frame.mvDepth), mBowVec(frame.mBowVec), mFeatVec(frame.mFeatVec),
     mDescriptors(frame.mDescriptors.clone()), mDescriptorsRight(frame.mDescriptorsRight.clone()),
     mvpMapPoints(frame.mvpMapPoints), mvbOutlier(frame.mvbOutlier), mImuCalib(frame.mImuCalib), mnCloseMPs(frame.mnCloseMPs),
     mpImuPreintegrated(frame.mpImuPreintegrated), mpImuPreintegratedFrame(frame.mpImuPreintegratedFrame), mImuBias(frame.mImuBias),
     mnId(frame.mnId), mpReferenceKF(frame.mpReferenceKF), mnScaleLevels(frame.mnScaleLevels),
     mfScaleFactor(frame.mfScaleFactor), mfLogScaleFactor(frame.mfLogScaleFactor),
     mvScaleFactors(frame.mvScaleFactors), mvInvScaleFactors(frame.mvInvScaleFactors), mNameFile(frame.mNameFile), mnDataset(frame.mnDataset),
     mvLevelSigma2(frame.mvLevelSigma2), mvInvLevelSigma2(frame.mvInvLevelSigma2), mpPrevFrame(frame.mpPrevFrame), mpLastKeyFrame(frame.mpLastKeyFrame), mbImuPreintegrated(frame.mbImuPreintegrated), mpMutexImu(frame.mpMutexImu),
     mpCamera(frame.mpCamera), mpCamera2(frame.mpCamera2), Nleft(frame.Nleft), Nright(frame.Nright),
     monoLeft(frame.monoLeft), monoRight(frame.monoRight), mvLeftToRightMatch(frame.mvLeftToRightMatch),
     mvRightToLeftMatch(frame.mvRightToLeftMatch), mvStereo3Dpoints(frame.mvStereo3Dpoints),
     mTlr(frame.mTlr.clone()), mRlr(frame.mRlr.clone()), mtlr(frame.mtlr.clone()), mTrl(frame.mTrl.clone()),
     mTrlx(frame.mTrlx), mTlrx(frame.mTlrx), mOwx(frame.mOwx), mRcwx(frame.mRcwx), mtcwx(frame.mtcwx), mpPythonClient(frame.mpPythonClient)
{
    for(int i=0;i<FRAME_GRID_COLS;i++)
        for(int j=0; j<FRAME_GRID_ROWS; j++){
            mGrid[i][j]=frame.mGrid[i][j];
            if(frame.Nleft > 0){
                mGridRight[i][j] = frame.mGridRight[i][j];
            }
        }

    if(!frame.mTcw.empty())
        SetPose(frame.mTcw);

    if(!frame.mVw.empty())
        mVw = frame.mVw.clone();

    mmProjectPoints = frame.mmProjectPoints;
    mmMatchedInImage = frame.mmMatchedInImage;

#ifdef REGISTER_TIMES
    mTimeStereoMatch = frame.mTimeStereoMatch;
    mTimeORB_Ext = frame.mTimeORB_Ext;
#endif
}


Frame::Frame(const cv::Mat &imLeft, const cv::Mat &imRight, const double &timeStamp, ORBextractor* extractorLeft, ORBextractor* extractorRight, ORBVocabulary* voc, cv::Mat &K, cv::Mat &distCoef, const float &bf, const float &thDepth, GeometricCamera* pCamera, Frame* pPrevF, const IMU::Calib &ImuCalib)
    :mpcpi(NULL), mpORBvocabulary(voc),mpORBextractorLeft(extractorLeft),mpORBextractorRight(extractorRight), mTimeStamp(timeStamp), mK(K.clone()), mDistCoef(distCoef.clone()), mbf(bf), mThDepth(thDepth),
     mImuCalib(ImuCalib), mpImuPreintegrated(NULL), mpPrevFrame(pPrevF),mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false),
     mpCamera(pCamera) ,mpCamera2(nullptr)
{
    // Step 1 帧的ID 自增
    mnId=nNextId++;

    // Step 2 计算图像金字塔的参数 
	//获取图像金字塔的层数
    mnScaleLevels = mpORBextractorLeft->GetLevels();
	//获得层与层之间的缩放比
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();
	//计算上面缩放比的对数
    mfLogScaleFactor = log(mfScaleFactor);
	//获取每层图像的缩放因子
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();
	//同样获取每层图像缩放因子的倒数
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();
	//高斯模糊的时候，使用的方差
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
	//获取sigma^2的倒数
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();

    // ORB extraction
	// Step 3 对左目右目图像提取ORB特征点, 第一个参数0-左图， 1-右图。为加速计算，同时开了两个线程计算
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartExtORB = std::chrono::steady_clock::now();
#endif
    thread threadLeft(&Frame::ExtractORB,this,0,imLeft,0,0);
	// 对右目图像提取orb特征
	thread threadRight(&Frame::ExtractORB,this,1,imRight,0,0);
	//等待两张图像特征点提取过程完成
    threadLeft.join();
    threadRight.join();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndExtORB = std::chrono::steady_clock::now();

    mTimeORB_Ext = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndExtORB - time_StartExtORB).count();
#endif

	//mvKeys中保存的是左图像中的特征点，这里是获取左侧图像中特征点的个数
    N = mvKeys.size();

	//如果左图像中没有成功提取到特征点那么就返回，也意味这这一帧的图像无法使用
    if(mvKeys.empty())
        return;
	
    // Step 4 用OpenCV的矫正函数、内参对提取到的特征点进行矫正
    UndistortKeyPoints();

#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartStereoMatches = std::chrono::steady_clock::now();
#endif
	// Step 5 计算双目间特征点的匹配，只有匹配成功的特征点会计算其深度,深度存放在 mvDepth 
	// mvuRight中存储的应该是左图像中的点所匹配的在右图像中的点的横坐标（纵坐标相同）
    ComputeStereoMatches();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndStereoMatches = std::chrono::steady_clock::now();

    mTimeStereoMatch = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndStereoMatches - time_StartStereoMatches).count();
#endif

	// 初始化本帧的地图点
    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(NULL));
    mvbOutlier = vector<bool>(N,false);
    mmProjectPoints.clear();// = map<long unsigned int, cv::Point2f>(N, static_cast<cv::Point2f>(NULL));
    mmMatchedInImage.clear();


    // This is done only for the first Frame (or after a change in the calibration)
	//  Step 5 计算去畸变后图像边界，将特征点分配到网格中。这个过程一般是在第一帧或者是相机标定参数发生变化之后进行
    if(mbInitialComputations)
    {
		//计算去畸变后图像的边界
        ComputeImageBounds(imLeft);

		// 表示一个图像像素相当于多少个图像网格列（宽）
        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/(mnMaxX-mnMinX);
		// 表示一个图像像素相当于多少个图像网格行（高）
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/(mnMaxY-mnMinY);



        fx = K.at<float>(0,0);
        fy = K.at<float>(1,1);
        cx = K.at<float>(0,2);
        cy = K.at<float>(1,2);
		// 猜测是因为这种除法计算需要的时间略长，所以这里直接存储了这个中间计算结果
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;
        imgH = imLeft.rows;
        imgW = imLeft.cols;
		//特殊的初始化过程完成，标志复位
        mbInitialComputations=false;
    }

    // 双目相机基线长度
    mb = mbf/fx;

    if(pPrevF)
    {
        if(!pPrevF->mVw.empty())
            mVw = pPrevF->mVw.clone();
    }
    else
    {
        mVw = cv::Mat::zeros(3,1,CV_32F);
    }
    // Step 6 将特征点分配到图像网格中 
    AssignFeaturesToGrid();

    mpMutexImu = new std::mutex();

    //Set no stereo fisheye information
    Nleft = -1;
    Nright = -1;
    mvLeftToRightMatch = vector<int>(0);
    mvRightToLeftMatch = vector<int>(0);
    mTlr = cv::Mat(3,4,CV_32F);
    mTrl = cv::Mat(3,4,CV_32F);
    mvStereo3Dpoints = vector<cv::Mat>(0);
    monoLeft = -1;
    monoRight = -1;
}


Frame::Frame(const cv::Mat &imLeft, const cv::Mat &imRight, const cv::Mat &imgrgb, PythonClient* P, const double &timeStamp, ORBextractor* extractorLeft, ORBextractor* extractorRight, ORBVocabulary* voc, cv::Mat &K, cv::Mat &distCoef, const float &bf, const float &thDepth, GeometricCamera* pCamera, Tracking *pTracker, Frame* pPrevF, const IMU::Calib &ImuCalib)
        :mpcpi(NULL), mpORBvocabulary(voc),mpORBextractorLeft(extractorLeft),mpORBextractorRight(extractorRight), mTimeStamp(timeStamp), mK(K.clone()), mDistCoef(distCoef.clone()), mbf(bf), mThDepth(thDepth),
         mImuCalib(ImuCalib), mpImuPreintegrated(NULL), mpPrevFrame(pPrevF),mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false),
         mpCamera(pCamera) ,mpCamera2(nullptr), mpPythonClient(P), mTracker(pTracker)
{
    // Step 1 帧的ID 自增
    mnId=nNextId++;

    mimgray = imLeft.clone();
//    imgLeft = imLeft;
//    imgRGB = imgrgb;

    // Step 2 计算图像金字塔的参数
    //获取图像金字塔的层数
    mnScaleLevels = mpORBextractorLeft->GetLevels();
    //获得层与层之间的缩放比
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();
    //计算上面缩放比的对数
    mfLogScaleFactor = log(mfScaleFactor);
    //获取每层图像的缩放因子
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();
    //同样获取每层图像缩放因子的倒数
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();
    //高斯模糊的时候，使用的方差
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
    //获取sigma^2的倒数
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();

    // ORB extraction
    std::chrono::steady_clock::time_point t11 = std::chrono::steady_clock::now();

    ExtractORBKeyPoints(0, imLeft);
    ExtractORBKeyPoints(1, imRight);

    //////////////


    cv::Mat  imGrayT = imLeft.clone();

    // Calculate the dynamic abnormal points and output the T matrix
    if(imGrayPre.data)
    {
//        std::chrono::steady_clock::time_point tm1 = std::chrono::steady_clock::now();
        ProcessMovingObject(imLeft);
//        std::chrono::steady_clock::time_point tm2 = std::chrono::steady_clock::now();
//        movingDetectTime= std::chrono::duration_cast<std::chrono::duration<double> >(tm2 - tm1).count();
        std::swap(imGrayPre, imGrayT);
    }
    else
    {
        std::swap(imGrayPre, imGrayT);
        flag_mov=0;
    }

////////////////////
    if(!T_M.empty())
    {
        //CheckMovingKeyPOints()是通过分割图和潜在动态点T擦除关键点mvKeysT中位于特定标签人身上的动态点
        flag_mov = CheckMovingKeyPoints(imgrgb, imLeft,mvKeysTemp,T_M);
//        cout << "check time =" << tc*1000 <<  endl;
    }
    std::chrono::steady_clock::time_point t22 = std::chrono::steady_clock::now();
    orbExtractTime= std::chrono::duration_cast<std::chrono::duration<double> >(t22 - t11).count();
    cout<<"orbExtractTime: "<<orbExtractTime<<endl;

    ExtractORBDesp(0,imLeft, 0, 0);
    ExtractORBDesp(1,imRight, 0, 0);


    /////下面是原来的版本，这里需要注释
    //mvKeys中保存的是左图像中的特征点，这里是获取左侧图像中特征点的个数
    N = mvKeys.size();

    //如果左图像中没有成功提取到特征点那么就返回，也意味这这一帧的图像无法使用
    if(mvKeys.empty())
        return;
    // Step 4 用OpenCV的矫正函数、内参对提取到的特征点进行矫正
    UndistortKeyPoints();

#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartStereoMatches = std::chrono::steady_clock::now();
#endif
    // Step 5 计算双目间特征点的匹配，只有匹配成功的特征点会计算其深度,深度存放在 mvDepth
    // mvuRight中存储的应该是左图像中的点所匹配的在右图像中的点的横坐标（纵坐标相同）
    ComputeStereoMatches();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndStereoMatches = std::chrono::steady_clock::now();

mTimeStereoMatch = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndStereoMatches - time_StartStereoMatches).count();
#endif

    // 初始化本帧的地图点
    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(NULL));
    mvbOutlier = vector<bool>(N,false);
    mmProjectPoints.clear();// = map<long unsigned int, cv::Point2f>(N, static_cast<cv::Point2f>(NULL));
    mmMatchedInImage.clear();


    // This is done only for the first Frame (or after a change in the calibration)
    //  Step 5 计算去畸变后图像边界，将特征点分配到网格中。这个过程一般是在第一帧或者是相机标定参数发生变化之后进行
    if(mbInitialComputations)
    {
        cerr << "in Frame::InitializeClass" << endl;
        //计算去畸变后图像的边界
        ComputeImageBounds(imLeft);

        // 表示一个图像像素相当于多少个图像网格列（宽）
        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/(mnMaxX-mnMinX);
        // 表示一个图像像素相当于多少个图像网格行（高）
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/(mnMaxY-mnMinY);



        fx = K.at<float>(0,0);
        fy = K.at<float>(1,1);
        cx = K.at<float>(0,2);
        cy = K.at<float>(1,2);
        // 猜测是因为这种除法计算需要的时间略长，所以这里直接存储了这个中间计算结果
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;
        imgH = imLeft.rows;
        imgW = imLeft.cols;
        //特殊的初始化过程完成，标志复位
        mbInitialComputations=false;
    }

    // 双目相机基线长度
    mb = mbf/fx;

    if(pPrevF)
    {
        if(!pPrevF->mVw.empty())
            mVw = pPrevF->mVw.clone();
    }
    else
    {
        mVw = cv::Mat::zeros(3,1,CV_32F);
    }
    // Step 6 将特征点分配到图像网格中
    AssignFeaturesToGrid();

    mpMutexImu = new std::mutex();

    //Set no stereo fisheye information
    Nleft = -1;
    Nright = -1;
    mvLeftToRightMatch = vector<int>(0);
    mvRightToLeftMatch = vector<int>(0);
    mTlr = cv::Mat(3,4,CV_32F);
    mTrl = cv::Mat(3,4,CV_32F);
    mvStereo3Dpoints = vector<cv::Mat>(0);
    monoLeft = -1;
    monoRight = -1;
}


Frame::Frame(const cv::Mat &imGray, const cv::Mat &imDepth, const double &timeStamp, ORBextractor* extractor,ORBVocabulary* voc, cv::Mat &K, cv::Mat &distCoef, const float &bf, const float &thDepth, GeometricCamera* pCamera,Frame* pPrevF, const IMU::Calib &ImuCalib)
    :mpcpi(NULL),mpORBvocabulary(voc),mpORBextractorLeft(extractor),mpORBextractorRight(static_cast<ORBextractor*>(NULL)),
     mTimeStamp(timeStamp), mK(K.clone()),mDistCoef(distCoef.clone()), mbf(bf), mThDepth(thDepth),
     mImuCalib(ImuCalib), mpImuPreintegrated(NULL), mpPrevFrame(pPrevF), mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false),
     mpCamera(pCamera),mpCamera2(nullptr)
{
    // Step 1 帧的ID 自增
    mnId=nNextId++;

    // Step 2 计算图像金字塔的参数
    mnScaleLevels = mpORBextractorLeft->GetLevels(); //获取图像金字塔的层数
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();   //获取每层的缩放因子
    mfLogScaleFactor = log(mfScaleFactor);	//计算每层缩放因子的自然对数
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();	//获取各层图像的缩放因子
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();	//获取各层图像的缩放因子的倒数
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();	//计算上面获取的sigma^2的倒数

    // ORB extraction
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartExtORB = std::chrono::steady_clock::now();
#endif
    ExtractORB(0, imGray,0,0);
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndExtORB = std::chrono::steady_clock::now();

    mTimeORB_Ext = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndExtORB - time_StartExtORB).count();
#endif
	//获取特征点的个数

    N = mvKeys.size();

	//如果这一帧没有能够提取出特征点，那么就直接返回了
    if(mvKeys.empty())
        return;

	// Step 4 用OpenCV的矫正函数、内参对提取到的特征点进行矫正
    UndistortKeyPoints();

	// Step 5 获取图像的深度，并且根据这个深度推算其右图中匹配的特征点的视差
    ComputeStereoFromRGBD(imDepth);

    // 初始化本帧的地图点
    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(NULL));

    mmProjectPoints.clear();// = map<long unsigned int, cv::Point2f>(N, static_cast<cv::Point2f>(NULL));
    mmMatchedInImage.clear();
	// 记录地图点是否为外点，初始化均为外点false
    mvbOutlier = vector<bool>(N,false);

    // This is done only for the first Frame (or after a change in the calibration)
	//  Step 5 计算去畸变后图像边界，将特征点分配到网格中。这个过程一般是在第一帧或者是相机标定参数发生变化之后进行
    if(mbInitialComputations)
    {
		//计算去畸变后图像的边界
        ComputeImageBounds(imGray);

        // 表示一个图像像素相当于多少个图像网格列（宽）
        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/static_cast<float>(mnMaxX-mnMinX);
		// 表示一个图像像素相当于多少个图像网格行（高）
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/static_cast<float>(mnMaxY-mnMinY);

        fx = K.at<float>(0,0);
        fy = K.at<float>(1,1);
        cx = K.at<float>(0,2);
        cy = K.at<float>(1,2);
		// 猜测是因为这种除法计算需要的时间略长，所以这里直接存储了这个中间计算结果
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;

		//特殊的初始化过程完成，标志复位
        mbInitialComputations=false;
    }

    // 计算假想的基线长度 baseline= mbf/fx
    // 后面要对从RGBD相机输入的特征点,结合相机基线长度,焦距,以及点的深度等信息来计算其在假想的"右侧图像"上的匹配点
    mb = mbf/fx;

    mpMutexImu = new std::mutex();

    //Set no stereo fisheye information
    Nleft = -1;
    Nright = -1;
    mvLeftToRightMatch = vector<int>(0);
    mvRightToLeftMatch = vector<int>(0);
    mTlr = cv::Mat(3,4,CV_32F);
    mTrl = cv::Mat(3,4,CV_32F);
    mvStereo3Dpoints = vector<cv::Mat>(0);
    monoLeft = -1;
    monoRight = -1;
	// 将特征点分配到图像网格中
    AssignFeaturesToGrid();
}


Frame::Frame(const cv::Mat &imGray, const double &timeStamp, ORBextractor* extractor,ORBVocabulary* voc, GeometricCamera* pCamera, cv::Mat &distCoef, const float &bf, const float &thDepth, Frame* pPrevF, const IMU::Calib &ImuCalib)
    :mpcpi(NULL),mpORBvocabulary(voc),mpORBextractorLeft(extractor),mpORBextractorRight(static_cast<ORBextractor*>(NULL)),
     mTimeStamp(timeStamp), mK(static_cast<Pinhole*>(pCamera)->toK()), mDistCoef(distCoef.clone()), mbf(bf), mThDepth(thDepth),
     mImuCalib(ImuCalib), mpImuPreintegrated(NULL),mpPrevFrame(pPrevF),mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false), mpCamera(pCamera),
     mpCamera2(nullptr)
{
    // Frame ID
	// Step 1 帧的ID 自增
    mnId=nNextId++;

    // Step 2 计算图像金字塔的参数 
    // Scale Level Info
	//获取图像金字塔的层数
    mnScaleLevels = mpORBextractorLeft->GetLevels();
	//获取每层的缩放因子
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();
	//计算每层缩放因子的自然对数
    mfLogScaleFactor = log(mfScaleFactor);
	//获取各层图像的缩放因子
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();
	//获取各层图像的缩放因子的倒数
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();
	//获取sigma^2
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
	//获取sigma^2的倒数
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();

    // ORB extraction
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartExtORB = std::chrono::steady_clock::now();
#endif
	// Step 3 对这个单目图像进行提取特征点, 第一个参数0-左图， 1-右图
    ExtractORB(0,imGray,0,1000);
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndExtORB = std::chrono::steady_clock::now();

    mTimeORB_Ext = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndExtORB - time_StartExtORB).count();
#endif
	
    //提取特征点的个数
    N = mvKeys.size();
	//如果没有能够成功提取出特征点，那么就直接返回了
    if(mvKeys.empty())
        return;
    // Step 4 用OpenCV的矫正函数、内参对提取到的特征点进行矫正 
    UndistortKeyPoints();

    // Set no stereo information
	// 由于单目相机无法直接获得立体信息，所以这里要给右图像对应点和深度赋值-1表示没有相关信息
    mvuRight = vector<float>(N,-1);
    mvDepth = vector<float>(N,-1);
    mnCloseMPs = 0;

    // 初始化本帧的地图点
    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(NULL));

    mmProjectPoints.clear();// = map<long unsigned int, cv::Point2f>(N, static_cast<cv::Point2f>(NULL));
    mmMatchedInImage.clear();
	// 记录地图点是否为外点，初始化均为外点false
    mvbOutlier = vector<bool>(N,false);

	//  Step 5 计算去畸变后图像边界，将特征点分配到网格中。这个过程一般是在第一帧或者是相机标定参数发生变化之后进行
    if(mbInitialComputations)
    {
		// 计算去畸变后图像的边界
        ComputeImageBounds(imGray);
		// 表示一个图像像素相当于多少个图像网格列（宽）
        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/static_cast<float>(mnMaxX-mnMinX);
		// 表示一个图像像素相当于多少个图像网格行（高）
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/static_cast<float>(mnMaxY-mnMinY);
        fx = static_cast<Pinhole*>(mpCamera)->toK().at<float>(0,0);
        fy = static_cast<Pinhole*>(mpCamera)->toK().at<float>(1,1);
        cx = static_cast<Pinhole*>(mpCamera)->toK().at<float>(0,2);
        cy = static_cast<Pinhole*>(mpCamera)->toK().at<float>(1,2);
		// 猜测是因为这种除法计算需要的时间略长，所以这里直接存储了这个中间计算结果
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;
		//特殊的初始化过程完成，标志复位
        mbInitialComputations=false;
    }

    //计算 basline
    mb = mbf/fx;

	// 将特征点分配到图像网格中 
    AssignFeaturesToGrid();

    //提取深度图
    if(pPrevF)
    {
        if(!pPrevF->mVw.empty())
            mVw = pPrevF->mVw.clone();
    }
    else
    {
        mVw = cv::Mat::zeros(3,1,CV_32F);
    }

}

Frame::Frame(const cv::Mat &imLeft, const cv::Mat &imRight, const cv::Mat &imDepth, const double &timeStamp, ORBextractor* extractorLeft, ORBextractor* extractorRight, ORBVocabulary* voc, cv::Mat &K, cv::Mat &distCoef, const float &bf, const float &thDepth, GeometricCamera* pCamera, Frame* pPrevF, const IMU::Calib &ImuCalib)
    :mpcpi(NULL),mpORBvocabulary(voc),mpORBextractorLeft(extractorLeft),mpORBextractorRight(extractorRight),mTimeStamp(timeStamp), mK(K.clone()),mDistCoef(distCoef.clone()), mbf(bf),mThDepth(thDepth),
     mImuCalib(ImuCalib), mpImuPreintegrated(NULL), mpPrevFrame(pPrevF),mpImuPreintegratedFrame(NULL),mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false),
     mpCamera(pCamera) ,mpCamera2(nullptr)
{
     // Step 1 帧的ID 自增
     mnId=nNextId++;

    // Step 2 计算图像金字塔的参数
    mnScaleLevels = mpORBextractorLeft->GetLevels(); //获取图像金字塔的层数
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();   //获取每层的缩放因子
    mfLogScaleFactor = log(mfScaleFactor);	//计算每层缩放因子的自然对数
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();	//获取各层图像的缩放因子
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();	//获取各层图像的缩放因子的倒数
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();	//计算上面获取的sigma^2的倒数

    // ORB extraction
    // Step 3 对左目右目图像提取ORB特征点, 第一个参数0-左图， 1-右图。为加速计算，同时开了两个线程计算
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartExtORB = std::chrono::steady_clock::now();
#endif
    thread threadLeft(&Frame::ExtractORB,this,0,imLeft,0,0);
    // 对右目图像提取orb特征
    thread threadRight(&Frame::ExtractORB,this,1,imRight,0,0);
    //等待两张图像特征点提取过程完成
    threadLeft.join();
    threadRight.join();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndExtORB = std::chrono::steady_clock::now();

    mTimeORB_Ext = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndExtORB - time_StartExtORB).count();
#endif

    //mvKeys中保存的是左图像中的特征点，这里是获取左侧图像中特征点的个数
    N = mvKeys.size();

    //如果左图像中没有成功提取到特征点那么就返回，也意味这这一帧的图像无法使用
    if(mvKeys.empty())
        return;

    // Step 4 用OpenCV的矫正函数、内参对提取到的特征点进行矫正
    UndistortKeyPoints();

    // Step 5 计算双目间特征点的匹配，只有匹配成功的特征点会计算其深度,深度存放在 mvDepth
    // mvuRight中存储的应该是左图像中的点所匹配的在右图像中的点的横坐标（纵坐标相同）
    ComputeStereoMatches();

    // Step 5 获取图像的深度，并且根据这个深度推算其右图中匹配的特征点的视差
//    ComputeStereoFromRGBD(imDepth);

    // 初始化本帧的地图点
    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(NULL));

    mmProjectPoints.clear();// = map<long unsigned int, cv::Point2f>(N, static_cast<cv::Point2f>(NULL));
    mmMatchedInImage.clear();
    // 记录地图点是否为外点，初始化均为外点false
    mvbOutlier = vector<bool>(N,false);


    // This is done only for the first Frame (or after a change in the calibration)
    //  Step 5 计算去畸变后图像边界，将特征点分配到网格中。这个过程一般是在第一帧或者是相机标定参数发生变化之后进行
    if(mbInitialComputations)
    {
        //计算去畸变后图像的边界
        ComputeImageBounds(imLeft);

        // 表示一个图像像素相当于多少个图像网格列（宽）
        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/static_cast<float>(mnMaxX-mnMinX);
        // 表示一个图像像素相当于多少个图像网格行（高）
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/static_cast<float>(mnMaxY-mnMinY);

        fx = K.at<float>(0,0);
        fy = K.at<float>(1,1);
        cx = K.at<float>(0,2);
        cy = K.at<float>(1,2);
        // 猜测是因为这种除法计算需要的时间略长，所以这里直接存储了这个中间计算结果
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;
        imgH = imLeft.rows;
        imgW = imLeft.cols;

        //特殊的初始化过程完成，标志复位
        mbInitialComputations=false;
    }

    // 计算假想的基线长度 baseline= mbf/fx
    // 后面要对从RGBD相机输入的特征点,结合相机基线长度,焦距,以及点的深度等信息来计算其在假想的"右侧图像"上的匹配点
    mb = mbf/fx;

    mpMutexImu = new std::mutex();

    //Set no stereo fisheye information
    Nleft = -1;
    Nright = -1;
    mvLeftToRightMatch = vector<int>(0);
    mvRightToLeftMatch = vector<int>(0);
    mTlr = cv::Mat(3,4,CV_32F);
    mTrl = cv::Mat(3,4,CV_32F);
    mvStereo3Dpoints = vector<cv::Mat>(0);
    monoLeft = -1;
    monoRight = -1;
    // 将特征点分配到图像网格中
    AssignFeaturesToGrid();
}

void Frame::AssignFeaturesToGrid()
{
    // Fill matrix with points
	// Step 1  给存储特征点的网格数组 Frame::mGrid 预分配空间
    const int nCells = FRAME_GRID_COLS*FRAME_GRID_ROWS;

    int nReserve = 0.5f*N/(nCells);
	//开始对mGrid这个二维数组中的每一个vector元素遍历并预分配空间
    for(unsigned int i=0; i<FRAME_GRID_COLS;i++)
        for (unsigned int j=0; j<FRAME_GRID_ROWS;j++){
            mGrid[i][j].reserve(nReserve);
            if(Nleft != -1){
                mGridRight[i][j].reserve(nReserve);
            }
        }

    // Step 2 遍历每个特征点，将每个特征点在mvKeysUn中的索引值放到对应的网格mGrid中

    for(int i=0;i<N;i++)
    {
        const cv::KeyPoint &kp = (Nleft == -1) ? mvKeysUn[i]
                                                 : (i < Nleft) ? mvKeys[i]
                                                                 : mvKeysRight[i - Nleft];
		//存储某个特征点所在网格的网格坐标，nGridPosX范围：[0,FRAME_GRID_COLS], nGridPosY范围：[0,FRAME_GRID_ROWS]
        int nGridPosX, nGridPosY;
		// 计算某个特征点所在网格的网格坐标，如果找到特征点所在的网格坐标，记录在nGridPosX,nGridPosY里，返回true，没找到返回false
        if(PosInGrid(kp,nGridPosX,nGridPosY)){
            if(Nleft == -1 || i < Nleft)
				//如果找到特征点所在网格坐标，将这个特征点的索引添加到对应网格的数组mGrid中
                mGrid[nGridPosX][nGridPosY].push_back(i);
            else
                mGridRight[nGridPosX][nGridPosY].push_back(i - Nleft);
        }
    }
}

//void Frame::checkBoundingBox(map<string, vector<string>>  boundingboxinfo)
//    {
//        // remove dynamic object here
//        stringstream ts;
//        ts << fixed << setprecision(6) << mTimeStamp;
//        cout.precision(16);
//        cout << "checking boundingboxinfo for frame!"<< mTimeStamp << endl;
//
//        vector<string> bbinfo = boundingboxinfo[string(ts.str() + ".png")];
//        for(auto s:bbinfo)
//        {
////        这里是遍历每一张图片的检测框
//            stringstream bbinfo_(s);
//            cout << bbinfo_.str() << endl;
//            string xmin_;
//            string ymin_;
//            string xmax_;
//            string ymax_;
//            const char deli = ',';
//            getline(bbinfo_, xmin_, deli);
//            getline(bbinfo_, ymin_, deli);
//            getline(bbinfo_, xmax_, deli);
//            getline(bbinfo_, ymax_, deli);
//            cout << xmin_ << " " << ymin_ << " " << xmax_ << " " << ymax_<< endl;
//            string::size_type st;
//            double xmin = stod(xmin_, &st);
//            double ymin = stod(ymin_, &st);
//            double xmax = stod(xmax_, &st);
//            double ymax = stod(ymax_, &st);
//            cout << xmin << " " << ymin << " " << xmax << " " << ymax << endl;
//
//            // check if match points is in boundingbox
//            // 遍历当前帧中的所有地图点
//            for(int i = 0;i<N;i++)
//            {
//                if(mvpMapPoints[i])
//                {
//                    cv::Mat p_ = mvpMapPoints[i]->GetWorldPos();
//
//                    const cv::Mat pc_ = mRcw * p_ + mtcw;     //这个就是[XC,YC,ZC,1]的转置=[[R,t],[O,1]]*[XW,YW,ZW,1]的转置,R就是一个1*3的向量，意义就是地图点的世界坐标
//                    //[XC,YC,ZC]是地图点在相机坐标系下的坐标,[XW,YW,ZW]是在世界坐标系下的坐标
////          cout<<pc_<<endl;
//                    const float pcx_ = pc_.at<float>(0);
//                    const float pcy_ = pc_.at<float>(1);
//                    const float pcz_ = pc_.at<float>(2);
////          cout<<pcx_<<" "<<pcy_<<" "<<pcz_<<endl;
//
//                    const float invz_ = 1.0 / pcz_;
//                    const float px = fx*pcx_*invz_ + cx;     //px = fx*pcx/pcz，这里实现的是相机坐标系到像素坐标系下的转化，求出在像素坐标系下x的坐标
//                    const float py = fy*pcy_*invz_ + cy;     //py = fy*pcy/pcz
//                    cout<<px<<" "<<py<<endl;
//                    if (px > xmin && px < xmax && py > ymin && py < ymax)
//                    {
//                        mvbOutlier[i] = true;
//                        cout << "find a removal point" << endl;
//                        dyPoint[i] = true;
//                    }
//
////          }
//                }
//            }
////        cv::rectangle(mimgray, cv::Point(xmin, ymin), cv::Point(xmax, ymax), (0, 0, 255), cv::LINE_4);
//
//        }
////    cv::imshow("img", mimgray);
////    cv::waitKey(1);
//
//
//}

void Frame::ExtractORB(int flag, const cv::Mat &im, const int x0, const int x1)
{
    vector<int> vLapping = {x0,x1};
	// 判断是左图还是右图
    if(flag==0)
		// 左图的话就套使用左图指定的特征点提取器，并将提取结果保存到对应的变量中 
        monoLeft = (*mpORBextractorLeft)(im,cv::Mat(),mvKeys,mDescriptors,vLapping);
    else
		// 右图的话就需要使用右图指定的特征点提取器，并将提取结果保存到对应的变量中 
        monoRight = (*mpORBextractorRight)(im,cv::Mat(),mvKeysRight,mDescriptorsRight,vLapping);
}

//ds-slam中计算关键点
void Frame::ExtractORBKeyPoints(int flag,const cv::Mat &imgray)
{
    if(flag==0)
    {
        (*mpORBextractorLeft)( imgray,cv::Mat(),mvKeysTemp);
    }
    else
        (*mpORBextractorRight)(imgray,cv::Mat(),mvKeysTempRight);
}

//ds-slam中计算描述子
void Frame::ExtractORBDesp(int flag,const cv::Mat &imgray, const int x0, const int x1)
{
    vector<int> vLapping = {x0,x1};
    if(flag==0)
        (*mpORBextractorLeft).ProcessDesp(imgray,cv::Mat(),mvKeysTemp,mvKeys,mDescriptors, vLapping);
    else
        (*mpORBextractorRight).ProcessDesp(imgray,cv::Mat(),mvKeysTempRight,mvKeysRight,mDescriptorsRight, vLapping);

}

void Frame::SetPose(cv::Mat Tcw)
{
    mTcw = Tcw.clone();
    UpdatePoseMatrices();
}

void Frame::GetPose(cv::Mat &Tcw)
{
    Tcw = mTcw.clone();
}

void Frame::SetNewBias(const IMU::Bias &b)
{
    mImuBias = b;
    if(mpImuPreintegrated)
        mpImuPreintegrated->SetNewBias(b);
}

void Frame::SetVelocity(const cv::Mat &Vwb)
{
    mVw = Vwb.clone();
}
/**
 * @brief 设置IMU的速度预测位姿，最后用imu到相机的位姿和世界坐标系到imu的位姿更新相机和世界坐标系的位姿
 *        目的：得到相机在世界坐标系的位姿
 * 
 * @param[in] Rwb     旋转值
 * @param[in] twb     位移值  
 * @param[in] Vwb     速度值
 */
void Frame::SetImuPoseVelocity(const cv::Mat &Rwb, const cv::Mat &twb, const cv::Mat &Vwb)
{
    //Twb -> Tbw 求逆的过程，根据旋转矩阵的逆求得，参见十四讲（第一版）P44公式（3.13）
    mVw = Vwb.clone();
    cv::Mat Rbw = Rwb.t();
    cv::Mat tbw = -Rbw*twb;
    cv::Mat Tbw = cv::Mat::eye(4,4,CV_32F);
    Rbw.copyTo(Tbw.rowRange(0,3).colRange(0,3));
    tbw.copyTo(Tbw.rowRange(0,3).col(3));
    // 外参值mImuCalib.Tcb与Tbw求mTcw
    mTcw = mImuCalib.Tcb*Tbw;
    UpdatePoseMatrices();
}


//根据Tcw计算mRcw、mtcw和mRwc、mOw
void Frame::UpdatePoseMatrices()
{
    // mOw：    当前相机光心在世界坐标系下坐标
    // mTcw：   世界坐标系到相机坐标系的变换矩阵
    // mRcw：   世界坐标系到相机坐标系的旋转矩阵
    // mtcw：   世界坐标系到相机坐标系的平移向量
    // mRwc：   相机坐标系到世界坐标系的旋转矩阵

	//从变换矩阵中提取出旋转矩阵
    //注意，rowRange这个只取到范围的左边界，而不取右边界
    mRcw = mTcw.rowRange(0,3).colRange(0,3);

    // mRcw求逆即可
    mRwc = mRcw.t();

    // 从变换矩阵中提取出旋转矩阵
    mtcw = mTcw.rowRange(0,3).col(3);

    // mTcw 求逆后是当前相机坐标系变换到世界坐标系下，对应的光心变换到世界坐标系下就是 mTcw的逆 中对应的平移向量
    mOw = -mRcw.t()*mtcw;

    // Static matrix
    mOwx =  cv::Matx31f(mOw.at<float>(0), mOw.at<float>(1), mOw.at<float>(2));
    mRcwx = cv::Matx33f(mRcw.at<float>(0,0), mRcw.at<float>(0,1), mRcw.at<float>(0,2),
                        mRcw.at<float>(1,0), mRcw.at<float>(1,1), mRcw.at<float>(1,2),
                        mRcw.at<float>(2,0), mRcw.at<float>(2,1), mRcw.at<float>(2,2));
    mtcwx = cv::Matx31f(mtcw.at<float>(0), mtcw.at<float>(1), mtcw.at<float>(2));

}

cv::Mat Frame::GetImuPosition()
{
    return mRwc*mImuCalib.Tcb.rowRange(0,3).col(3)+mOw;
}

cv::Mat Frame::GetImuRotation()
{
    return mRwc*mImuCalib.Tcb.rowRange(0,3).colRange(0,3);
}

cv::Mat Frame::GetImuPose()
{
    cv::Mat Twb = cv::Mat::eye(4,4,CV_32F);
    Twb.rowRange(0,3).colRange(0,3) = mRwc*mImuCalib.Tcb.rowRange(0,3).colRange(0,3);
    Twb.rowRange(0,3).col(3) = mRwc*mImuCalib.Tcb.rowRange(0,3).col(3)+mOw;
    return Twb.clone();
}

/**
 * @brief 判断路标点是否在视野中
 * 步骤
 * Step 1 获得这个地图点的世界坐标
 * Step 2 关卡一：检查这个地图点在当前帧的相机坐标系下，是否有正的深度.如果是负的，表示出错，返回false
 * Step 3 关卡二：将MapPoint投影到当前帧的像素坐标(u,v), 并判断是否在图像有效范围内
 * Step 4 关卡三：计算MapPoint到相机中心的距离, 并判断是否在尺度变化的距离内
 * Step 5 关卡四：计算当前相机指向地图点向量和地图点的平均观测方向夹角的余弦值, 若小于设定阈值，返回false
 * Step 6 根据地图点到光心的距离来预测一个尺度（仿照特征点金字塔层级）
 * Step 7 记录计算得到的一些参数
 * @param[in] pMP                       当前地图点
 * @param[in] viewingCosLimit           夹角余弦，用于限制地图点和光心连线和法线的夹角
 * @return true                         地图点合格，且在视野内
 * @return false                        地图点不合格，抛弃
 */
bool Frame::isInFrustum(MapPoint *pMP, float viewingCosLimit)
{
    if(Nleft == -1){
        // cout << "\na";
		// mbTrackInView是决定一个地图点是否进行重投影的标志
    	// 这个标志的确定要经过多个函数的确定，isInFrustum()只是其中的一个验证关卡。这里默认设置为否
        pMP->mbTrackInView = false;
        pMP->mTrackProjX = -1;
        pMP->mTrackProjY = -1;

        // 3D in absolute coordinates
		// Step 1 获得这个地图点的世界坐标
        cv::Matx31f Px = pMP->GetWorldPos2();

        // cout << "b";

        // 3D in camera coordinates
		// 根据当前帧(粗糙)位姿转化到当前相机坐标系下的三维点Pc
        const cv::Matx31f Pc = mRcwx * Px + mtcwx;
        const float Pc_dist = cv::norm(Pc);

        // Check positive depth
        const float &PcZ = Pc(2);
        const float invz = 1.0f/PcZ;
    	// Step 2 关卡一：检查这个地图点在当前帧的相机坐标系下，是否有正的深度.如果是负的，表示出错，直接返回false
        if(PcZ<0.0f)
            return false;

        const cv::Point2f uv = mpCamera->project(Pc);
    	// Step 3 关卡二：将MapPoint投影到当前帧的像素坐标(u,v), 并判断是否在图像有效范围内
        // cout << "c";
    	// 判断是否在图像边界中，只要不在那么就说明无法在当前帧下进行重投影
        if(uv.x<mnMinX || uv.x>mnMaxX)
            return false;
        if(uv.y<mnMinY || uv.y>mnMaxY)
            return false;

        // cout << "d";
        pMP->mTrackProjX = uv.x;
        pMP->mTrackProjY = uv.y;

        // Check distance is in the scale invariance region of the MapPoint
    	// Step 4 关卡三：计算MapPoint到相机中心的距离, 并判断是否在尺度变化的距离内
     	// 得到认为的可靠距离范围:[0.8f*mfMinDistance, 1.2f*mfMaxDistance]
        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
    	// 得到当前地图点距离当前帧相机光心的距离,注意P，mOw都是在同一坐标系下才可以
    	//  mOw：当前相机光心在世界坐标系下坐标
        const cv::Matx31f PO = Px-mOwx;
		//取模就得到了距离
        const float dist = cv::norm(PO);

		//如果不在允许的尺度变化范围内，认为重投影不可靠
        if(dist<minDistance || dist>maxDistance)
            return false;

        // cout << "e";

        // Check viewing angle
    	// Step 5 关卡四：计算当前相机指向地图点向量和地图点的平均观测方向夹角的余弦值, 若小于cos(viewingCosLimit), 即夹角大于viewingCosLimit弧度则返回
        cv::Matx31f Pnx = pMP->GetNormal2();

        // cout << "f";
		// 计算当前相机指向地图点向量和地图点的平均观测方向夹角的余弦值，注意平均观测方向为单位向量
        const float viewCos = PO.dot(Pnx)/dist;

		//如果大于给定的阈值 cos(60°)=0.5，认为这个点方向太偏了，重投影不可靠，返回false
        if(viewCos<viewingCosLimit)
            return false;

        // Predict scale in the image
    	// Step 6 根据地图点到光心的距离来预测一个尺度（仿照特征点金字塔层级）
        const int nPredictedLevel = pMP->PredictScale(dist,this);

        // cout << "g";
    	// Step 7 记录计算得到的一些参数
        // Data used by the tracking
    	// 通过置位标记 MapPoint::mbTrackInView 来表示这个地图点要被投影 
        pMP->mbTrackInView = true;
    	// 该地图点投影在当前图像（一般是左图）的像素横坐标
        pMP->mTrackProjX = uv.x;
    	// bf/z其实是视差，相减得到右图（如有）中对应点的横坐标
        pMP->mTrackProjXR = uv.x - mbf*invz;

        pMP->mTrackDepth = Pc_dist;
        // cout << "h";
		// 该地图点投影在当前图像（一般是左图）的像素纵坐标									
        pMP->mTrackProjY = uv.y;
    	// 根据地图点到光心距离，预测的该地图点的尺度层级
        pMP->mnTrackScaleLevel= nPredictedLevel;
    	// 保存当前视角和法线夹角的余弦值
        pMP->mTrackViewCos = viewCos;

        // cout << "i";
    	//执行到这里说明这个地图点在相机的视野中并且进行重投影是可靠的，返回true
        return true;
    }
    else{
        pMP->mbTrackInView = false;
        pMP->mbTrackInViewR = false;
        pMP -> mnTrackScaleLevel = -1;
        pMP -> mnTrackScaleLevelR = -1;

        pMP->mbTrackInView = isInFrustumChecks(pMP,viewingCosLimit);
        pMP->mbTrackInViewR = isInFrustumChecks(pMP,viewingCosLimit,true);

        return pMP->mbTrackInView || pMP->mbTrackInViewR;
    }
}

bool Frame::ProjectPointDistort(MapPoint* pMP, cv::Point2f &kp, float &u, float &v)
{

    // 3D in absolute coordinates
    cv::Mat P = pMP->GetWorldPos();

    // 3D in camera coordinates
    const cv::Mat Pc = mRcw*P+mtcw;
    const float &PcX = Pc.at<float>(0);
    const float &PcY= Pc.at<float>(1);
    const float &PcZ = Pc.at<float>(2);

    // Check positive depth
    if(PcZ<0.0f)
    {
        cout << "Negative depth: " << PcZ << endl;
        return false;
    }

    // Project in image and check it is not outside
    const float invz = 1.0f/PcZ;
    u=fx*PcX*invz+cx;
    v=fy*PcY*invz+cy;

    // cout << "c";

    if(u<mnMinX || u>mnMaxX)
        return false;
    if(v<mnMinY || v>mnMaxY)
        return false;

    float u_distort, v_distort;

    float x = (u - cx) * invfx;
    float y = (v - cy) * invfy;
    float r2 = x * x + y * y;
    float k1 = mDistCoef.at<float>(0);
    float k2 = mDistCoef.at<float>(1);
    float p1 = mDistCoef.at<float>(2);
    float p2 = mDistCoef.at<float>(3);
    float k3 = 0;
    if(mDistCoef.total() == 5)
    {
        k3 = mDistCoef.at<float>(4);
    }

    // Radial distorsion
    float x_distort = x * (1 + k1 * r2 + k2 * r2 * r2 + k3 * r2 * r2 * r2);
    float y_distort = y * (1 + k1 * r2 + k2 * r2 * r2 + k3 * r2 * r2 * r2);

    // Tangential distorsion
    x_distort = x_distort + (2 * p1 * x * y + p2 * (r2 + 2 * x * x));
    y_distort = y_distort + (p1 * (r2 + 2 * y * y) + 2 * p2 * x * y);

    u_distort = x_distort * fx + cx;
    v_distort = y_distort * fy + cy;


    u = u_distort;
    v = v_distort;

    kp = cv::Point2f(u, v);

    return true;
}

cv::Mat Frame::inRefCoordinates(cv::Mat pCw)
{
    return mRcw*pCw+mtcw;
}

vector<size_t> Frame::GetFeaturesInArea(const float &x, const float  &y, const float  &r, const int minLevel, const int maxLevel, const bool bRight) const
{
	// 存储搜索结果的vector
    vector<size_t> vIndices;
    vIndices.reserve(N);

    float factorX = r;
    float factorY = r;

    // Step 1 计算半径为r圆左右上下边界所在的网格列和行的id
    // 查找半径为r的圆左侧边界所在网格列坐标。这个地方有点绕，慢慢理解下：
    // (mnMaxX-mnMinX)/FRAME_GRID_COLS：表示列方向每个网格可以平均分得几个像素（肯定大于1）
    // mfGridElementWidthInv=FRAME_GRID_COLS/(mnMaxX-mnMinX) 是上面倒数，表示每个像素可以均分几个网格列（肯定小于1）
	// (x-mnMinX-r)，可以看做是从图像的左边界mnMinX到半径r的圆的左边界区域占的像素列数
	// 两者相乘，就是求出那个半径为r的圆的左侧边界在哪个网格列中
    // 保证nMinCellX 结果大于等于0
    const int nMinCellX = max(0,(int)floor((x-mnMinX-factorX)*mfGridElementWidthInv));
	// 如果最终求得的圆的左边界所在的网格列超过了设定了上限，那么就说明计算出错，找不到符合要求的特征点，返回空vector
    if(nMinCellX>=FRAME_GRID_COLS)
    {
        return vIndices;
    }
	// 计算圆所在的右边界网格列索引
    const int nMaxCellX = min((int)FRAME_GRID_COLS-1,(int)ceil((x-mnMinX+factorX)*mfGridElementWidthInv));
	// 如果计算出的圆右边界所在的网格不合法，说明该特征点不好，直接返回空vector
    if(nMaxCellX<0)
    {
        return vIndices;
    }
	//后面的操作也都是类似的，计算出这个圆上下边界所在的网格行的id
    const int nMinCellY = max(0,(int)floor((y-mnMinY-factorY)*mfGridElementHeightInv));
    if(nMinCellY>=FRAME_GRID_ROWS)
    {
        return vIndices;
    }

    const int nMaxCellY = min((int)FRAME_GRID_ROWS-1,(int)ceil((y-mnMinY+factorY)*mfGridElementHeightInv));
    if(nMaxCellY<0)
    {
        return vIndices;
    }
    // 检查需要搜索的图像金字塔层数范围是否符合要求
    //? 疑似bug。(minLevel>0) 后面条件 (maxLevel>=0)肯定成立
    //? 改为 const bool bCheckLevels = (minLevel>=0) || (maxLevel>=0);
    const bool bCheckLevels = (minLevel>0) || (maxLevel>=0);

    // Step 2 遍历圆形区域内的所有网格，寻找满足条件的候选特征点，并将其index放到输出里
    for(int ix = nMinCellX; ix<=nMaxCellX; ix++)
    {
        for(int iy = nMinCellY; iy<=nMaxCellY; iy++)
        {
            // 获取这个网格内的所有特征点在 Frame::mvKeysUn 中的索引
            const vector<size_t> vCell = (!bRight) ? mGrid[ix][iy] : mGridRight[ix][iy];
			// 如果这个网格中没有特征点，那么跳过这个网格继续下一个
            if(vCell.empty())
                continue;

            // 如果这个网格中有特征点，那么遍历这个图像网格中所有的特征点
            for(size_t j=0, jend=vCell.size(); j<jend; j++)
            {
				// 根据索引先读取这个特征点 
                const cv::KeyPoint &kpUn = (Nleft == -1) ? mvKeysUn[vCell[j]]
                                                         : (!bRight) ? mvKeys[vCell[j]]
                                                                     : mvKeysRight[vCell[j]];
                if(bCheckLevels)
                {
					// cv::KeyPoint::octave中表示的是从金字塔的哪一层提取的数据
					// 保证特征点是在金字塔层级minLevel和maxLevel之间，不是的话跳过
                    if(kpUn.octave<minLevel)
                        continue;
                    if(maxLevel>=0)
                        if(kpUn.octave>maxLevel)
                            continue;
                }

                // 通过检查，计算候选特征点到圆中心的距离，查看是否是在这个圆形区域之内
                const float distx = kpUn.pt.x-x;
                const float disty = kpUn.pt.y-y;

				// 如果x方向和y方向的距离都在指定的半径之内，存储其index为候选特征点
                if(fabs(distx)<factorX && fabs(disty)<factorY)
                    vIndices.push_back(vCell[j]);
            }
        }
    }

    return vIndices;
}


/**
 * @brief 计算某个特征点所在网格的网格坐标，如果找到特征点所在的网格坐标，记录在nGridPosX,nGridPosY里，返回true，没找到返回false
 * 
 * @param[in] kp                    给定的特征点
 * @param[in & out] posX            特征点所在网格坐标的横坐标
 * @param[in & out] posY            特征点所在网格坐标的纵坐标
 * @return true                     如果找到特征点所在的网格坐标，返回true
 * @return false                    没找到返回false
 */
bool Frame::PosInGrid(const cv::KeyPoint &kp, int &posX, int &posY)
{
	// 计算特征点x,y坐标落在哪个网格内，网格坐标为posX，posY
    // mfGridElementWidthInv=(FRAME_GRID_COLS)/(mnMaxX-mnMinX);
    // mfGridElementHeightInv=(FRAME_GRID_ROWS)/(mnMaxY-mnMinY);
    posX = round((kp.pt.x-mnMinX)*mfGridElementWidthInv);
    posY = round((kp.pt.y-mnMinY)*mfGridElementHeightInv);

    //Keypoint's coordinates are undistorted, which could cause to go out of the image
    // 因为特征点进行了去畸变，而且前面计算是round取整，所以有可能得到的点落在图像网格坐标外面
    // 如果网格坐标posX，posY超出了[0,FRAME_GRID_COLS] 和[0,FRAME_GRID_ROWS]，表示该特征点没有对应网格坐标，返回false
    if(posX<0 || posX>=FRAME_GRID_COLS || posY<0 || posY>=FRAME_GRID_ROWS)
        return false;

	// 计算成功返回true
    return true;
}

/**
 * @brief 计算当前帧特征点对应的词袋Bow，主要是mBowVec 和 mFeatVec
 * 
 */
void Frame::ComputeBoW()
{
    // 判断是否以前已经计算过了，计算过了就跳过
    if(mBowVec.empty())
    {
		// 将描述子mDescriptors转换为DBOW要求的输入格式
        vector<cv::Mat> vCurrentDesc = Converter::toDescriptorVector(mDescriptors);
		// 将特征点的描述子转换成词袋向量mBowVec以及特征向量mFeatVec
        mpORBvocabulary->transform(vCurrentDesc,	//当前的描述子vector
								   mBowVec,			//输出，词袋向量，记录的是单词的id及其对应权重TF-IDF值
								   mFeatVec,		//输出，记录node id及其对应的图像 feature对应的索引
								   4);				//4表示从叶节点向前数的层数
    }
}

/**
 * @brief 用内参对特征点去畸变，结果报存在mvKeysUn中
 * 
 */
void Frame::UndistortKeyPoints()
{
    // Step 1 如果第一个畸变参数为0，不需要矫正。第一个畸变参数k1是最重要的，一般不为0，为0的话，说明畸变参数都是0
	//变量mDistCoef中存储了opencv指定格式的去畸变参数，格式为：(k1,k2,p1,p2,k3)
    if(mDistCoef.at<float>(0)==0.0)
    {
        mvKeysUn=mvKeys;
        return;
    }


    // Step 2 如果畸变参数不为0，用OpenCV函数进行畸变矫正
    // Fill matrix with points
    // N为提取的特征点数量，为满足OpenCV函数输入要求，将N个特征点保存在N*2的矩阵中
    cv::Mat mat(N,2,CV_32F);
	//遍历每个特征点，并将它们的坐标保存到矩阵中
    for(int i=0; i<N; i++)
    {
		//然后将这个特征点的横纵坐标分别保存
        mat.at<float>(i,0)=mvKeys[i].pt.x;
        mat.at<float>(i,1)=mvKeys[i].pt.y;
    }

    // Undistort points
    // 函数reshape(int cn,int rows=0) 其中cn为更改后的通道数，rows=0表示这个行将保持原来的参数不变
    //为了能够直接调用opencv的函数来去畸变，需要先将矩阵调整为2通道（对应坐标x,y）
    // cv::undistortPoints最后一个矩阵为空矩阵时，得到的点为归一化坐标点
    mat=mat.reshape(2);
    cv::undistortPoints(mat,mat, static_cast<Pinhole*>(mpCamera)->toK(),mDistCoef,cv::Mat(),mK);
	//调整回只有一个通道，回归我们正常的处理方式
    mat=mat.reshape(1);


    // Fill undistorted keypoint vector
    // Step 3 存储校正后的特征点
    mvKeysUn.resize(N);
	//遍历每一个特征点
    for(int i=0; i<N; i++)
    {
		//根据索引获取这个特征点
		//注意之所以这样做而不是直接重新声明一个特征点对象的目的是，能够得到源特征点对象的其他属性
        cv::KeyPoint kp = mvKeys[i];
		//读取校正后的坐标并覆盖老坐标
        kp.pt.x=mat.at<float>(i,0);
        kp.pt.y=mat.at<float>(i,1);
        mvKeysUn[i]=kp;
    }

}

/**
 * @brief 计算去畸变图像的边界
 * 
 * @param[in] imLeft            需要计算边界的图像
 */
void Frame::ComputeImageBounds(const cv::Mat &imLeft)
{
    // 如果畸变参数不为0，用OpenCV函数进行畸变矫正
    if(mDistCoef.at<float>(0)!=0.0)
    {
        // 保存矫正前的图像四个边界点坐标： (0,0) (cols,0) (0,rows) (cols,rows)
        cv::Mat mat(4,2,CV_32F);
        mat.at<float>(0,0)=0.0;         //左上
		mat.at<float>(0,1)=0.0;
        mat.at<float>(1,0)=imLeft.cols; //右上
		mat.at<float>(1,1)=0.0;
		mat.at<float>(2,0)=0.0;         //左下
		mat.at<float>(2,1)=imLeft.rows;
        mat.at<float>(3,0)=imLeft.cols; //右下
		mat.at<float>(3,1)=imLeft.rows;

        // Undistort corners
		// 和前面校正特征点一样的操作，将这几个边界点作为输入进行校正
        mat=mat.reshape(2);
        cv::undistortPoints(mat,mat,static_cast<Pinhole*>(mpCamera)->toK(),mDistCoef,cv::Mat(),mK);
        mat=mat.reshape(1);

        // Undistort corners
		//校正后的四个边界点已经不能够围成一个严格的矩形，因此在这个四边形的外侧加边框作为坐标的边界
        mnMinX = min(mat.at<float>(0,0),mat.at<float>(2,0));//左上和左下横坐标最小的
        mnMaxX = max(mat.at<float>(1,0),mat.at<float>(3,0));//右上和右下横坐标最大的
        mnMinY = min(mat.at<float>(0,1),mat.at<float>(1,1));//左上和右上纵坐标最小的
        mnMaxY = max(mat.at<float>(2,1),mat.at<float>(3,1));//左下和右下纵坐标最小的
    }
    else
    {
        // 如果畸变参数为0，就直接获得图像边界
        mnMinX = 0.0f;
        mnMaxX = imLeft.cols;
        mnMinY = 0.0f;
        mnMaxY = imLeft.rows;
    }
}

/*
 * 双目匹配函数
 *
 * 为左图的每一个特征点在右图中找到匹配点 \n
 * 根据基线(有冗余范围)上描述子距离找到匹配, 再进行SAD精确定位 \n ‘
 * 这里所说的SAD是一种双目立体视觉匹配算法，可参考[https://blog.csdn.net/u012507022/article/details/51446891]
 * 最后对所有SAD的值进行排序, 剔除SAD值较大的匹配对，然后利用抛物线拟合得到亚像素精度的匹配 \n 
 * 这里所谓的亚像素精度，就是使用这个拟合得到一个小于一个单位像素的修正量，这样可以取得更好的估计结果，计算出来的点的深度也就越准确
 * 匹配成功后会更新 mvuRight(ur) 和 mvDepth(Z)
 */
void Frame::ComputeStereoMatches()
{
    /*两帧图像稀疏立体匹配（即：ORB特征点匹配，非逐像素的密集匹配，但依然满足行对齐）
     * 输入：两帧立体矫正后的图像img_left 和 img_right 对应的orb特征点集
     * 过程：
          1. 行特征点统计. 统计img_right每一行上的ORB特征点集，便于使用立体匹配思路(行搜索/极线搜索）进行同名点搜索, 避免逐像素的判断.
          2. 粗匹配. 根据步骤1的结果，对img_left第i行的orb特征点pi，在img_right的第i行上的orb特征点集中搜索相似orb特征点, 得到qi
          3. 精确匹配. 以点qi为中心，半径为r的范围内，进行块匹配（归一化SAD），进一步优化匹配结果
          4. 亚像素精度优化. 步骤3得到的视差为uchar/int类型精度，并不一定是真实视差，通过亚像素差值（抛物线插值)获取float精度的真实视差
          5. 最优视差值/深度选择. 通过胜者为王算法（WTA）获取最佳匹配点。
          6. 删除离缺点(outliers). 块匹配相似度阈值判断，归一化sad最小，并不代表就一定是正确匹配，比如光照变化、弱纹理等会造成误匹配
     * 输出：稀疏特征点视差图/深度图（亚像素精度）mvDepth 匹配结果 mvuRight
     */

    // 为匹配结果预先分配内存，数据类型为float型
    // mvuRight存储右图匹配点索引
    // mvDepth存储特征点的深度信息
    mvuRight = vector<float>(N,-1.0f);
    mvDepth = vector<float>(N,-1.0f);

	// orb特征相似度阈值  -> mean ～= (max  + min) / 2
    const int thOrbDist = (ORBmatcher::TH_HIGH+ORBmatcher::TH_LOW)/2;

    // 金字塔顶层（0层）图像高 nRows
    const int nRows = mpORBextractorLeft->mvImagePyramid[0].rows;

	// 二维vector存储每一行的orb特征点的列坐标的索引，为什么是vector，因为每一行的特征点有可能不一样，例如
    // vRowIndices[0] = [1，2，5，8, 11]   第1行有5个特征点,他们的列号（即x坐标）分别是1,2,5,8,11
    // vRowIndices[1] = [2，6，7，9, 13, 17, 20]  第2行有7个特征点.etc
    vector<vector<size_t> > vRowIndices(nRows,vector<size_t>());

    for(int i=0; i<nRows; i++)
        vRowIndices[i].reserve(200);
	// 右图特征点数量，N表示数量 r表示右图，且不能被修改
    const int Nr = mvKeysRight.size();

	// Step 1. 行特征点统计. 考虑到尺度金字塔特征，一个特征点可能存在于多行，而非唯一的一行
    for(int iR=0; iR<Nr; iR++)
    {
        // 获取特征点ir的y坐标，即行号
        const cv::KeyPoint &kp = mvKeysRight[iR];
        const float &kpY = kp.pt.y;
        // 计算特征点ir在行方向上，可能的偏移范围r，即可能的行号为[kpY + r, kpY -r]
        // 2 表示在全尺寸(scale = 1)的情况下，假设有2个像素的偏移，随着尺度变化，r也跟着变化
        const float r = 2.0f*mvScaleFactors[mvKeysRight[iR].octave];
        const int maxr = ceil(kpY+r);
        const int minr = floor(kpY-r);

        // 将特征点ir保证在可能的行号中
        for(int yi=minr;yi<=maxr;yi++)
            vRowIndices[yi].push_back(iR);
    }

    // Step 2 -> 3. 粗匹配 + 精匹配
    // 对于立体矫正后的两张图，在列方向(x)存在最大视差maxd和最小视差mind
    // 也即是左图中任何一点p，在右图上的匹配点的范围为应该是[p - maxd, p - mind], 而不需要遍历每一行所有的像素
    // maxd = baseline * length_focal / minZ
    // mind = baseline * length_focal / maxZ
    // Set limits for search
    const float minZ = mb;
    const float minD = 0;
    const float maxD = mbf/minZ;

    // For each left keypoint search a match in the right image
	// 保存sad块匹配相似度和左图特征点索引
    vector<pair<int, int> > vDistIdx;
    vDistIdx.reserve(N);

    // 为左图每一个特征点il，在右图搜索最相似的特征点ir
    for(int iL=0; iL<N; iL++)
    {
        const cv::KeyPoint &kpL = mvKeys[iL];
        const int &levelL = kpL.octave;
        const float &vL = kpL.pt.y;
        const float &uL = kpL.pt.x;

        // 获取左图特征点il所在行，以及在右图对应行中可能的匹配点
        const vector<size_t> &vCandidates = vRowIndices[vL];

        if(vCandidates.empty())
            continue;
        // 计算理论上的最佳搜索范围
        const float minU = uL-maxD;
        const float maxU = uL-minD;

        // 最大搜索范围小于0，说明无匹配点
        if(maxU<0)
            continue;
		// 初始化最佳相似度，用最大相似度，以及最佳匹配点索引
        int bestDist = ORBmatcher::TH_HIGH;
        size_t bestIdxR = 0;

        const cv::Mat &dL = mDescriptors.row(iL);

        // Step2. 粗配准. 左图特征点il与右图中的可能的匹配点进行逐个比较,得到最相似匹配点的相似度和索引
        for(size_t iC=0; iC<vCandidates.size(); iC++)
        {
            const size_t iR = vCandidates[iC];
            const cv::KeyPoint &kpR = mvKeysRight[iR];

            // 左图特征点il与带匹配点ic的空间尺度差超过2，放弃
            if(kpR.octave<levelL-1 || kpR.octave>levelL+1)
                continue;

            // 使用列坐标(x)进行匹配，和stereomatch一样
            const float &uR = kpR.pt.x;

            // 超出理论搜索范围[minU, maxU]，可能是误匹配，放弃
            if(uR>=minU && uR<=maxU)
            {
                // 计算匹配点il和待匹配点ic的相似度dist
                const cv::Mat &dR = mDescriptorsRight.row(iR);
                const int dist = ORBmatcher::DescriptorDistance(dL,dR);

				//统计最小相似度及其对应的列坐标(x)
                if(dist<bestDist)
                {
                    bestDist = dist;
                    bestIdxR = iR;
                }
            }
        }

        // Subpixel match by correlation
        // 如果刚才匹配过程中的最佳描述子距离小于给定的阈值
        // Step 3. 精确匹配. 
        if(bestDist<thOrbDist)
        {
            // coordinates in image pyramid at keypoint scale
			// 计算右图特征点x坐标和对应的金字塔尺度
            const float uR0 = mvKeysRight[bestIdxR].pt.x;
            const float scaleFactor = mvInvScaleFactors[kpL.octave];
            // 尺度缩放后的左右图特征点坐标
            const float scaleduL = round(kpL.pt.x*scaleFactor);
            const float scaledvL = round(kpL.pt.y*scaleFactor);
            const float scaleduR0 = round(uR0*scaleFactor);

            // 滑动窗口搜索, 类似模版卷积或滤波
            // w表示sad相似度的窗口半径
            const int w = 5;
            // 提取左图中，以特征点(scaleduL,scaledvL)为中心, 半径为w的图像快patch
            cv::Mat IL = mpORBextractorLeft->mvImagePyramid[kpL.octave].rowRange(scaledvL-w,scaledvL+w+1).colRange(scaleduL-w,scaleduL+w+1);

			//初始化最佳相似度
            int bestDist = INT_MAX;
			// 通过滑动窗口搜索优化，得到的列坐标偏移量
            int bestincR = 0;
			//滑动窗口的滑动范围为（-L, L）
            const int L = 5;
			// 初始化存储图像块相似度
            vector<float> vDists;
            vDists.resize(2*L+1);

            // 计算滑动窗口滑动范围的边界，因为是块匹配，还要算上图像块的尺寸
            // 列方向起点 iniu = r0 + 最大窗口滑动范围 - 图像块尺寸
            // 列方向终点 eniu = r0 + 最大窗口滑动范围 + 图像块尺寸 + 1
            // 此次 + 1 和下面的提取图像块是列坐标+1是一样的，保证提取的图像块的宽是2 * w + 1
            const float iniu = scaleduR0+L-w;
            const float endu = scaleduR0+L+w+1;
			// 判断搜索是否越界
            if(iniu<0 || endu >= mpORBextractorRight->mvImagePyramid[kpL.octave].cols)
                continue;

			// 在搜索范围内从左到右滑动，并计算图像块相似度
            for(int incR=-L; incR<=+L; incR++)
            {
                // 提取左图中，以特征点(scaleduL,scaledvL)为中心, 半径为w的图像快patch
                cv::Mat IR = mpORBextractorRight->mvImagePyramid[kpL.octave].rowRange(scaledvL-w,scaledvL+w+1).colRange(scaleduR0+incR-w,scaleduR0+incR+w+1);

                // sad 计算
                float dist = cv::norm(IL,IR,cv::NORM_L1);
                // 统计最小sad和偏移量
                if(dist<bestDist)
                {
                    bestDist =  dist;
                    bestincR = incR;
                }

                //L+incR 为refine后的匹配点列坐标(x)
                vDists[L+incR] = dist;
            }

            // 搜索窗口越界判断ß 
            if(bestincR==-L || bestincR==L)
                continue;

			// Step 4. 亚像素插值, 使用最佳匹配点及其左右相邻点构成抛物线
            // 使用3点拟合抛物线的方式，用极小值代替之前计算的最优是差值
            //    \                 / <- 由视差为14，15，16的相似度拟合的抛物线
            //      .             .(16)
            //         .14     .(15) <- int/uchar最佳视差值
            //              . 
            //           （14.5）<- 真实的视差值
            //   deltaR = 15.5 - 16 = -0.5
            // 公式参考opencv sgbm源码中的亚像素插值公式
            // 或论文<<On Building an Accurate Stereo Matching System on Graphics Hardware>> 公式7
            // Sub-pixel match (Parabola fitting)
            const float dist1 = vDists[L+bestincR-1];
            const float dist2 = vDists[L+bestincR];
            const float dist3 = vDists[L+bestincR+1];

            const float deltaR = (dist1-dist3)/(2.0f*(dist1+dist3-2.0f*dist2));

            // 亚像素精度的修正量应该是在[-1,1]之间，否则就是误匹配
            if(deltaR<-1 || deltaR>1)
                continue;

            // 根据亚像素精度偏移量delta调整最佳匹配索引
            float bestuR = mvScaleFactors[kpL.octave]*((float)scaleduR0+(float)bestincR+deltaR);

            float disparity = (uL-bestuR);

            if(disparity>=minD && disparity<maxD)
            {

                // 如果存在负视差，则约束为0.01
                if(disparity<=0)
                {
                    disparity=0.01;
                    bestuR = uL-0.01;
                }
                // 根据视差值计算深度信息
                // 保存最相似点的列坐标(x)信息
                // 保存归一化sad最小相似度
                // Step 5. 最优视差值/深度选择.
                mvDepth[iL]=mbf/disparity;
                mvuRight[iL] = bestuR;
                vDistIdx.push_back(pair<int,int>(bestDist,iL));
            }
        }
    }
    // Step 6. 删除离缺点(outliers)
    // 块匹配相似度阈值判断，归一化sad最小，并不代表就一定是匹配的，比如光照变化、弱纹理、无纹理等同样会造成误匹配
    // 误匹配判断条件  norm_sad > 1.5 * 1.4 * median
    sort(vDistIdx.begin(),vDistIdx.end());
    const float median = vDistIdx[vDistIdx.size()/2].first;
    const float thDist = 1.5f*1.4f*median;

    for(int i=vDistIdx.size()-1;i>=0;i--)
    {
        if(vDistIdx[i].first<thDist)
            break;
        else
        {
			// 误匹配点置为-1，和初始化时保持一直，作为error code
            mvuRight[vDistIdx[i].second]=-1;
            mvDepth[vDistIdx[i].second]=-1;
        }
    }
}

//计算RGBD图像的立体深度信息
void Frame::ComputeStereoFromRGBD(const cv::Mat &imDepth)
{
    /** 主要步骤如下:.对于彩色图像中的每一个特征点:<ul>  */
    // mvDepth直接由depth图像读取`
	//这里是初始化这两个存储“右图”匹配特征点横坐标和存储特征点深度值的vector
    mvuRight = vector<float>(N,-1);
    mvDepth = vector<float>(N,-1);

	//开始遍历彩色图像中的所有特征点
    for(int i=0; i<N; i++)
    {
        /** <li> 从<b>未矫正的特征点</b>提供的坐标来读取深度图像拿到这个点的深度数据 </li> */
		//获取校正前和校正后的特征点
        const cv::KeyPoint &kp = mvKeys[i];
        const cv::KeyPoint &kpU = mvKeysUn[i];

		//获取其横纵坐标，注意 NOTICE 是校正前的特征点的
        const float &v = kp.pt.y;
        const float &u = kp.pt.x;
		//从深度图像中获取这个特征点对应的深度点
        //NOTE 从这里看对深度图像进行去畸变处理是没有必要的,我们依旧可以直接通过未矫正的特征点的坐标来直接拿到深度数据
        const float d = imDepth.at<float>(v,u);

		// 如果获取到的深度点合法(d>0), 那么就保存这个特征点的深度,并且计算出等效的\在假想的右图中该特征点所匹配的特征点的横坐标
        if(d>0)
        {
            mvDepth[i] = d;
            mvuRight[i] = kpU.pt.x-mbf/d;
        }
    }
}

//当某个特征点的深度信息或者双目信息有效时,将它反投影到三维世界坐标系中
cv::Mat Frame::UnprojectStereo(const int &i)
{
    const float z = mvDepth[i];
    if(z>0)
    {
        const float u = mvKeysUn[i].pt.x;
        const float v = mvKeysUn[i].pt.y;
        const float x = (u-cx)*z*invfx;
        const float y = (v-cy)*z*invfy;
        cv::Mat x3Dc = (cv::Mat_<float>(3,1) << x, y, z);
        return mRwc*x3Dc+mOw;
    }
    else
        return cv::Mat();
}

void Frame::ProcessMovingObject(const cv::Mat &imgray)
{

    // Clear the previous data
    F_prepoint.clear();
    F_nextpoint.clear();
    F2_prepoint.clear();
    F2_nextpoint.clear();
    T_M.clear();
    //计算Harris角点
    cv::goodFeaturesToTrack(imGrayPre, prepoint, 1000, 0.01, 8, cv::Mat(), 3, true, 0.04);

//    cv::Mat imGrayPre1 = imGrayPre.clone();
//    cv::Mat imGrayPre2 = imGrayPre.clone();
//    cv::Mat imGrayPre3 = imgray.clone();

    cv::cornerSubPix(imGrayPre, prepoint, cv::Size(10, 10), cv::Size(-1, -1), cv::TermCriteria(cv::TermCriteria::MAX_ITER | cv::TermCriteria::EPS, 20, 0.03));

//    cout<<"prepoint.size:"<<prepoint.size()<<endl;

    //step 2 Lucas-Kanade方法计算稀疏特征集的光流。计算光流金字塔，光流金字塔是光流法的一种常见的处理方式，
    // 能够避免位移较大时丢失追踪的情况，高博的十四讲里面有讲
    cv::calcOpticalFlowPyrLK(imGrayPre,  //输入图像1
                             imgray,  //输入图像2
                             prepoint,  //输入图像1的角点
                             nextpoint,  //输入图像2的角点
                             state,  // 记录光流点是否跟踪成功，成功status =1,否则为0
                             err,
                             cv::Size(15, 15),
                             3,  //5层金字塔
                             cv::TermCriteria(cv::TermCriteria::MAX_ITER | cv::TermCriteria::EPS, 10, 0.03));
//    cout<<"prepoint.size"<<prepoint.size()<<endl;
//    cout<<"nextpoint.size"<<nextpoint.size()<<endl;
//    cout<<prepoint<<endl;
//    cout<<nextpoint<<endl;


    //step 3 对于光流法得到的 角点进行筛选。筛选的结果放入 F_prepoint F_nextpoint 两个数组当中。
    // 光流角点是否跟踪成功保存在status数组当中
    //////////////
//    cout<<state.size()<<endl;
    for (int i = 0; i < state.size(); i++)
    {
        if(state[i] != 0)   // 光流跟踪成功的点
        {
            int dx[10] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };
            int dy[10] = { -1, -1, -1, 0, 0, 0, 1, 1, 1 };
            int x1 = prepoint[i].x, y1 = prepoint[i].y;
            int x2 = nextpoint[i].x, y2 = nextpoint[i].y;

            // 认为超过规定区域的,太靠近边缘。 跟踪的光流点的status 设置为0 ,一会儿会丢弃这些点
            if ((x1 < limit_edge_corner || x1 >= imgray.cols - limit_edge_corner || x2 < limit_edge_corner || x2 >= imgray.cols - limit_edge_corner
                 || y1 < limit_edge_corner || y1 >= imgray.rows - limit_edge_corner || y2 < limit_edge_corner || y2 >= imgray.rows - limit_edge_corner))
            {
                state[i] = 0;
                continue;
            }

            // 对于光流跟踪的结果进行验证，匹配对中心3*3的图像块的像素差（sum）太大，那么也舍弃这个匹配点
            double sum_check = 0;
            for (int j = 0; j < 9; j++)
                sum_check += abs(imGrayPre.at<uchar>(y1 + dy[j], x1 + dx[j]) - imgray.at<uchar>(y2 + dy[j], x2 + dx[j]));
            if (sum_check > limit_of_check) state[i] = 0;

            // 好的光流点存入 F_prepoint F_nextpoint 两个数组当中
            if (state[i])
            {
                F_prepoint.push_back(prepoint[i]);
                F_nextpoint.push_back(nextpoint[i]);
            }
        }
    }

//    cout<<"F_prepoint.size"<<F_prepoint.size()<<endl;
//    cout<<"F_nextpoint.size"<<F_nextpoint.size()<<endl;
//    cout<<F_prepoint<<endl;
//    cout<<F_nextpoint<<endl;

    // 根据筛选后的光流点计算F-矩阵
    cv::Mat mask = cv::Mat(cv::Size(1, 300), CV_8UC1);
    cv::Mat F = cv::findFundamentalMat(F_prepoint, F_nextpoint, mask, cv::FM_RANSAC, 0.1, 0.99);
    //这个是利用对极几何去更新F_prepoin和F_nextpoint
    ////对极几何更新有问题，更新前后的F_pre数量不变
    for (int i = 0; i < mask.rows; i++)
    {
        if (mask.at<uchar>(i, 0) == 0);
        else
        {
            // Circle(pre_frame, F_prepoint[i], 6, Scalar(255, 255, 0), 3);
            double A = F.at<double>(0, 0)*F_prepoint[i].x + F.at<double>(0, 1)*F_prepoint[i].y + F.at<double>(0, 2);
            double B = F.at<double>(1, 0)*F_prepoint[i].x + F.at<double>(1, 1)*F_prepoint[i].y + F.at<double>(1, 2);
            double C = F.at<double>(2, 0)*F_prepoint[i].x + F.at<double>(2, 1)*F_prepoint[i].y + F.at<double>(2, 2);
            double dd = fabs(A*F_nextpoint[i].x + B*F_nextpoint[i].y + C) / sqrt(A*A + B*B); //Epipolar constraints
            if (dd <= 0.1)
            {
                F2_prepoint.push_back(F_prepoint[i]);
                F2_nextpoint.push_back(F_nextpoint[i]);
            }
        }
    }
    F_prepoint = F2_prepoint;
    F_nextpoint = F2_nextpoint;

//    cout<<"对极几何更新后的F_prepoint.size"<<F_prepoint.size()<<endl;
//    cout<<"对极几何更新后的F_nextpoint.size"<<F_nextpoint.size()<<endl;
//    cout<<F_prepoint<<endl;
//    cout<<F_nextpoint<<endl;


    // 6 对第3步光流法生成的 nextpoint ，利用极线约束进行验证，并且不满足约束的放入T_M 矩阵，如果不满足约束 那应该就是动态点了
    for (int i = 0; i < prepoint.size(); i++)
    {
        if (state[i] != 0)
        {
            double A = F.at<double>(0, 0)*prepoint[i].x + F.at<double>(0, 1)*prepoint[i].y + F.at<double>(0, 2);
            double B = F.at<double>(1, 0)*prepoint[i].x + F.at<double>(1, 1)*prepoint[i].y + F.at<double>(1, 2);
            double C = F.at<double>(2, 0)*prepoint[i].x + F.at<double>(2, 1)*prepoint[i].y + F.at<double>(2, 2);
            // 点到直线的距离
            double dd = fabs(A*nextpoint[i].x + B*nextpoint[i].y + C) / sqrt(A*A + B*B);

//            cout<<i<<endl;
//            cout<<dd<<endl;

            // Judge outliers   认为大于 阈值的点是动态点，存入T_M
            if (dd <= limit_dis_epi) continue;      // 閾值大小是1
            T_M.push_back(nextpoint[i]);
        }
    }

}



//****
// 判断kp是否在bounding_box_(txt读入的数据)内. objects_cur_存储帧中的object,由frame.cc引入
// 判断特征点是否在所有的检测框内
bool Frame::IsInBox(const int& i, int& box_id) {
    const cv::KeyPoint& kp = mvKeysUn[i];
    float kp_u  = kp.pt.x;
    float kp_v = kp.pt.y;
    bool in_box = false;
    for (int k = 0; k < objects_cur_.size(); ++k) {     //遍历所有物体
        vector<double> box = objects_cur_[k]->vdetect_parameter;
        double left = box[0];
        double top = box[1];
        double right = box[2];
        double bottom = box[3];
        if (kp_u > left - 2 && kp_u < right + 2
            && kp_v > top - 2 && kp_v < bottom + 2) {
            in_box = true;
            box_id = k;
            break;
        }
    }
    return in_box;
}

//判断kp是否在bounding_box_(txt读入的数据)内. objects_cur_存储帧中的object,由frame.cc引入
//判断特征点是否在动态的目标框内
bool Frame::IsInDynamic(const int& i) {
    const cv::KeyPoint& kp = mvKeys[i];
    float kp_u  = kp.pt.x;
    float kp_v = kp.pt.y;
    bool in_dynamic = false;

    for (int k = 0; k < objects_cur_.size(); ++k) {//遍历所有物体
        int obj_class = objects_cur_[k]->ndetect_class;

        if (obj_class == 3){
            vector<double> box = objects_cur_[k]->vdetect_parameter;//获取物体框
            double left = box[0];
            double top = box[1];
            double right = box[2];
            double bottom = box[3];

            if (kp_u > left - 2 && kp_u < right + 2
                && kp_v > top - 2 && kp_v < bottom + 2) {
                in_dynamic = true;
            }
        }
    }
    return in_dynamic;
}

//判断kp是否在bounding_box_(txt读入的数据)内. objects_cur_存储帧中的object,由frame.cc引入
//判断特征点是否在静态的目标框内
bool Frame::IsInStatic(const int& i) {
    const cv::KeyPoint& kp = mvKeys[i];
    float kp_u  = kp.pt.x;
    float kp_v = kp.pt.y;
    bool in_static = false;

    for (int k = 0; k < objects_cur_.size(); ++k) {//遍历所有物体
        int obj_class = objects_cur_[k]->ndetect_class;

        if (obj_class == 1){
            vector<double> box = objects_cur_[k]->vdetect_parameter;//获取物体框
            double left = box[0];
            double top = box[1];
            double right = box[2];
            double bottom = box[3];

            if (kp_u > left - 2 && kp_u < right + 2
                && kp_v > top - 2 && kp_v < bottom + 2) {
                in_static = true;
            }
        }
    }
    return in_static;
}




//****

bool Frame::imuIsPreintegrated()
{
    unique_lock<std::mutex> lock(*mpMutexImu);
    return mbImuPreintegrated;
}

void Frame::setIntegrated()
{
    unique_lock<std::mutex> lock(*mpMutexImu);
    mbImuPreintegrated = true;
}

Frame::Frame(const cv::Mat &imLeft, const cv::Mat &imRight, const double &timeStamp, ORBextractor* extractorLeft, ORBextractor* extractorRight, ORBVocabulary* voc, cv::Mat &K, cv::Mat &distCoef, const float &bf, const float &thDepth, GeometricCamera* pCamera, GeometricCamera* pCamera2, cv::Mat& Tlr,Frame* pPrevF, const IMU::Calib &ImuCalib)
        :mpcpi(NULL), mpORBvocabulary(voc),mpORBextractorLeft(extractorLeft),mpORBextractorRight(extractorRight), mTimeStamp(timeStamp), mK(K.clone()), mDistCoef(distCoef.clone()), mbf(bf), mThDepth(thDepth),
         mImuCalib(ImuCalib), mpImuPreintegrated(NULL), mpPrevFrame(pPrevF),mpImuPreintegratedFrame(NULL), mpReferenceKF(static_cast<KeyFrame*>(NULL)), mbImuPreintegrated(false), mpCamera(pCamera), mpCamera2(pCamera2), mTlr(Tlr)
{
    imgLeft = imLeft.clone();
    imgRight = imRight.clone();

    // Frame ID
    mnId=nNextId++;

    // Scale Level Info
    mnScaleLevels = mpORBextractorLeft->GetLevels();
    mfScaleFactor = mpORBextractorLeft->GetScaleFactor();
    mfLogScaleFactor = log(mfScaleFactor);
    mvScaleFactors = mpORBextractorLeft->GetScaleFactors();
    mvInvScaleFactors = mpORBextractorLeft->GetInverseScaleFactors();
    mvLevelSigma2 = mpORBextractorLeft->GetScaleSigmaSquares();
    mvInvLevelSigma2 = mpORBextractorLeft->GetInverseScaleSigmaSquares();

    // ORB extraction
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartExtORB = std::chrono::steady_clock::now();
#endif
    thread threadLeft(&Frame::ExtractORB,this,0,imLeft,static_cast<KannalaBrandt8*>(mpCamera)->mvLappingArea[0],static_cast<KannalaBrandt8*>(mpCamera)->mvLappingArea[1]);
    thread threadRight(&Frame::ExtractORB,this,1,imRight,static_cast<KannalaBrandt8*>(mpCamera2)->mvLappingArea[0],static_cast<KannalaBrandt8*>(mpCamera2)->mvLappingArea[1]);
    threadLeft.join();
    threadRight.join();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndExtORB = std::chrono::steady_clock::now();

    mTimeORB_Ext = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndExtORB - time_StartExtORB).count();
#endif
    // 左图中提取的特征点数目
    Nleft = mvKeys.size();
    // 右图中提取的特征点数目
    Nright = mvKeysRight.size();
    // 特征点总数
    N = Nleft + Nright;

    if(N == 0)
        return;

    // This is done only for the first Frame (or after a change in the calibration)
    if(mbInitialComputations)
    {
        ComputeImageBounds(imLeft);

        mfGridElementWidthInv=static_cast<float>(FRAME_GRID_COLS)/(mnMaxX-mnMinX);
        mfGridElementHeightInv=static_cast<float>(FRAME_GRID_ROWS)/(mnMaxY-mnMinY);

        fx = K.at<float>(0,0);
        fy = K.at<float>(1,1);
        cx = K.at<float>(0,2);
        cy = K.at<float>(1,2);
        invfx = 1.0f/fx;
        invfy = 1.0f/fy;

        mbInitialComputations=false;
    }

    mb = mbf / fx;

    mRlr = mTlr.rowRange(0,3).colRange(0,3);
    mtlr = mTlr.col(3);

    cv::Mat Rrl = mTlr.rowRange(0,3).colRange(0,3).t();
    cv::Mat trl = Rrl * (-1 * mTlr.col(3));

    cv::hconcat(Rrl,trl,mTrl);

    mTrlx = cv::Matx34f(Rrl.at<float>(0,0), Rrl.at<float>(0,1), Rrl.at<float>(0,2), trl.at<float>(0),
                        Rrl.at<float>(1,0), Rrl.at<float>(1,1), Rrl.at<float>(1,2), trl.at<float>(1),
                        Rrl.at<float>(2,0), Rrl.at<float>(2,1), Rrl.at<float>(2,2), trl.at<float>(2));
    mTlrx = cv::Matx34f(mRlr.at<float>(0,0), mRlr.at<float>(0,1), mRlr.at<float>(0,2), mtlr.at<float>(0),
                        mRlr.at<float>(1,0), mRlr.at<float>(1,1), mRlr.at<float>(1,2), mtlr.at<float>(1),
                        mRlr.at<float>(2,0), mRlr.at<float>(2,1), mRlr.at<float>(2,2), mtlr.at<float>(2));

#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_StartStereoMatches = std::chrono::steady_clock::now();
#endif
    ComputeStereoFishEyeMatches();
#ifdef REGISTER_TIMES
    std::chrono::steady_clock::time_point time_EndStereoMatches = std::chrono::steady_clock::now();

    mTimeStereoMatch = std::chrono::duration_cast<std::chrono::duration<double,std::milli> >(time_EndStereoMatches - time_StartStereoMatches).count();
#endif

    //Put all descriptors in the same matrix
    cv::vconcat(mDescriptors,mDescriptorsRight,mDescriptors);

    mvpMapPoints = vector<MapPoint*>(N,static_cast<MapPoint*>(nullptr));
    mvbOutlier = vector<bool>(N,false);

    AssignFeaturesToGrid();

    mpMutexImu = new std::mutex();

    UndistortKeyPoints();
}

void Frame::ComputeStereoFishEyeMatches() {
    //Speed it up by matching keypoints in the lapping area
    vector<cv::KeyPoint> stereoLeft(mvKeys.begin() + monoLeft, mvKeys.end());
    vector<cv::KeyPoint> stereoRight(mvKeysRight.begin() + monoRight, mvKeysRight.end());

    cv::Mat stereoDescLeft = mDescriptors.rowRange(monoLeft, mDescriptors.rows);
    cv::Mat stereoDescRight = mDescriptorsRight.rowRange(monoRight, mDescriptorsRight.rows);

    mvLeftToRightMatch = vector<int>(Nleft,-1);
    mvRightToLeftMatch = vector<int>(Nright,-1);
    mvDepth = vector<float>(Nleft,-1.0f);
    mvuRight = vector<float>(Nleft,-1);
    mvStereo3Dpoints = vector<cv::Mat>(Nleft);
    mnCloseMPs = 0;

    //Perform a brute force between Keypoint in the left and right image
    vector<vector<cv::DMatch>> matches;

    BFmatcher.knnMatch(stereoDescLeft,stereoDescRight,matches,2);

    int nMatches = 0;
    int descMatches = 0;

    //Check matches using Lowe's ratio
    for(vector<vector<cv::DMatch>>::iterator it = matches.begin(); it != matches.end(); ++it){
        if((*it).size() >= 2 && (*it)[0].distance < (*it)[1].distance * 0.7){
            //For every good match, check parallax and reprojection error to discard spurious matches
            cv::Mat p3D;
            descMatches++;
            float sigma1 = mvLevelSigma2[mvKeys[(*it)[0].queryIdx + monoLeft].octave], sigma2 = mvLevelSigma2[mvKeysRight[(*it)[0].trainIdx + monoRight].octave];
            float depth = static_cast<KannalaBrandt8*>(mpCamera)->TriangulateMatches(mpCamera2,mvKeys[(*it)[0].queryIdx + monoLeft],mvKeysRight[(*it)[0].trainIdx + monoRight],mRlr,mtlr,sigma1,sigma2,p3D);
            if(depth > 0.0001f){
                mvLeftToRightMatch[(*it)[0].queryIdx + monoLeft] = (*it)[0].trainIdx + monoRight;
                mvRightToLeftMatch[(*it)[0].trainIdx + monoRight] = (*it)[0].queryIdx + monoLeft;
                mvStereo3Dpoints[(*it)[0].queryIdx + monoLeft] = p3D.clone();
                mvDepth[(*it)[0].queryIdx + monoLeft] = depth;
                nMatches++;
            }
        }
    }
}

bool Frame::isInFrustumChecks(MapPoint *pMP, float viewingCosLimit, bool bRight) {
    // 3D in absolute coordinates
    cv::Matx31f Px = pMP->GetWorldPos2();

    cv::Matx33f mRx;
    cv::Matx31f mtx, twcx;

    cv::Matx33f Rcw = mRcwx;
    cv::Matx33f Rwc = mRcwx.t();
    cv::Matx31f tcw = mOwx;

    if(bRight){
        cv::Matx33f Rrl = mTrlx.get_minor<3,3>(0,0);
        cv::Matx31f trl = mTrlx.get_minor<3,1>(0,3);
        mRx = Rrl * Rcw;
        mtx = Rrl * tcw + trl;
        twcx = Rwc * mTlrx.get_minor<3,1>(0,3) + tcw;
    }
    else{
        mRx = mRcwx;
        mtx = mtcwx;
        twcx = mOwx;
    }

    // 3D in camera coordinates

    cv::Matx31f Pcx = mRx * Px + mtx;
    const float Pc_dist = cv::norm(Pcx);
    const float &PcZ = Pcx(2);

    // Check positive depth
    if(PcZ<0.0f)
        return false;

    // Project in image and check it is not outside
    cv::Point2f uv;
    if(bRight) uv = mpCamera2->project(Pcx);
    else uv = mpCamera->project(Pcx);

    if(uv.x<mnMinX || uv.x>mnMaxX)
        return false;
    if(uv.y<mnMinY || uv.y>mnMaxY)
        return false;

    // Check distance is in the scale invariance region of the MapPoint
    const float maxDistance = pMP->GetMaxDistanceInvariance();
    const float minDistance = pMP->GetMinDistanceInvariance();
    const cv::Matx31f POx = Px - twcx;
    const float dist = cv::norm(POx);

    if(dist<minDistance || dist>maxDistance)
        return false;

    // Check viewing angle
    cv::Matx31f Pnx = pMP->GetNormal2();

    const float viewCos = POx.dot(Pnx)/dist;

    if(viewCos<viewingCosLimit)
        return false;

    // Predict scale in the image
    const int nPredictedLevel = pMP->PredictScale(dist,this);

    if(bRight){
        pMP->mTrackProjXR = uv.x;
        pMP->mTrackProjYR = uv.y;
        pMP->mnTrackScaleLevelR= nPredictedLevel;
        pMP->mTrackViewCosR = viewCos;
        pMP->mTrackDepthR = Pc_dist;
    }
    else{
        pMP->mTrackProjX = uv.x;
        pMP->mTrackProjY = uv.y;
        pMP->mnTrackScaleLevel= nPredictedLevel;
        pMP->mTrackViewCos = viewCos;
        pMP->mTrackDepth = Pc_dist;
    }

    return true;
}

cv::Mat Frame::UnprojectStereoFishEye(const int &i){
    return mRwc*mvStereo3Dpoints[i]+mOw;
}

int Frame::CheckMovingKeyPoints(const cv::Mat &imrgbd, const cv::Mat &imGray, std::vector<std::vector<cv::KeyPoint>>& mvKeysT,std::vector<cv::Point2f> T)
{
    cv::Mat imgbefore = imrgbd.clone();
    cv::Mat img_before_box = imrgbd.clone();
    cv::Mat imgafter = imrgbd.clone();
    cv::Mat img_after_box = imrgbd.clone();
    float scale;
    flag_orb_mov =0;
    nlevels = mpORBextractorLeft->nlevels;
    mvScaleFactor = mpORBextractorLeft->mvScaleFactor;


    cout << "checking boundingboxinfo for frame!"<< mTimeStamp << endl;

    ostringstream oss;
    oss << setw(6) << setfill('0') << mTimeStamp;


    bbinfo = mTracker->boundingboxinfo[string(oss.str() + ".png")];
    bbstate = vector<int>(bbinfo.size(), 0);
    count = vector<int>(bbinfo.size(), 0);

//    cout<<"bbinfo:"<<bbinfo.size()<<endl;
    int j = 0;
    for(auto s:bbinfo)
    {
//        cout<<j<<endl;
        stringstream bbinfo_(s);

        string xmin_;
        string ymin_;
        string xmax_;
        string ymax_;
        const char deli = ',';
        getline(bbinfo_, xmin_, deli);
        getline(bbinfo_, ymin_, deli);
        getline(bbinfo_, xmax_, deli);
        getline(bbinfo_, ymax_, deli);

        string::size_type st;

        double _xmin = stod(xmin_, &st);
        double _ymin = stod(ymin_, &st);
        double _xmax = stod(xmax_, &st);
        double _ymax = stod(ymax_, &st);
//        cout<<_xmin<<" "<<_ymin<<" "<<_xmax<<" "<<_ymax<<endl;

        for (int i = 0; i < T.size() ; i++)
        {
            /////T[i].x和T[i].y就是动态特征点在图像上的坐标,那么只需要判断特征点T[i]是否在检测框里就行了，如果在，就把检测框标记为动态目标
            //如果这个点在这个检测框内
            if(T[i].x > _xmin && T[i].x < _xmax && T[i].y > _ymin && T[i].y < _ymax)
            {
                flag_orb_mov = 1;
                count[j]++;
            }
            if(count[j]>=2)
            {
                bbstate[j] = 1;
            }

            //将动态点矩阵中的点画出来
            cv::rectangle(img_before_box,cv::Point(T[i].x-5, T[i].y-5), cv::Point(T[i].x+5, T[i].y+5),cv::Scalar(255,0,0));
            cv::circle(img_before_box,T[i],2,cv::Scalar(255,0,0),-1);

        }

        //将所有的检测框画出来
        cv::rectangle(imgbefore, cv::Point(_xmin, _ymin), cv::Point(_xmax, _ymax), cv::Scalar(0,0,255), 2);

        j++;
    }

//    cv::imwrite("img/before_box/"+to_string(mTimeStamp)+".png", img_before_box);
//    cv::imwrite("img/before/"+to_string(mTimeStamp)+".png", imgbefore);
//
//    imgafter = img_before_box.clone();
//    img_after_box = img_before_box.clone();
//
//    int k=0;
//    for(auto s:bbinfo)
//    {
//        stringstream bbinfo_(s);
//
//        string xmin_;
//        string ymin_;
//        string xmax_;
//        string ymax_;
//        const char deli = ',';
//        getline(bbinfo_, xmin_, deli);
//        getline(bbinfo_, ymin_, deli);
//        getline(bbinfo_, xmax_, deli);
//        getline(bbinfo_, ymax_, deli);
//
//        string::size_type st;
//
//        double _xmin = stod(xmin_, &st);
//        double _ymin = stod(ymin_, &st);
//        double _xmax = stod(xmax_, &st);
//        double _ymax = stod(ymax_, &st);
//
//        //将所有的检测框和动态点画一起
//        cv::rectangle(imgafter, cv::Point(_xmin, _ymin), cv::Point(_xmax, _ymax), cv::Scalar(0,0,255), 2);
//
//
//        //画最终的结果
//        if(bbstate[k]==1)
//        {
//            cv::rectangle(img_after_box, cv::Point(_xmin, _ymin), cv::Point(_xmax, _ymax), cv::Scalar(0,0,255), 2);
//        }
//        else
//        {
//            cv::rectangle(img_after_box, cv::Point(_xmin, _ymin), cv::Point(_xmax, _ymax), cv::Scalar(255,0,0), 2);
//        }
//
//        k++;
//    }
//
//    cv::imwrite("img/after/"+to_string(mTimeStamp)+".png", imgafter);
//    cv::imwrite("img/after_box/"+to_string(mTimeStamp)+".png", img_after_box);
    ///画红色框是动态目标，蓝色框是静态目标



    //moving
    int it = 0;
    if(flag_orb_mov==1)
    {
        for (auto s:bbinfo)
        {
            if(bbstate[it] == 1)
            {
                stringstream bbinfo_(s);
                string xmin_;
                string ymin_;
                string xmax_;
                string ymax_;
                const char deli = ',';
                getline(bbinfo_, xmin_, deli);
                getline(bbinfo_, ymin_, deli);
                getline(bbinfo_, xmax_, deli);
                getline(bbinfo_, ymax_, deli);
                string::size_type st;

                xmin = stod(xmin_, &st);
                ymin = stod(ymin_, &st);
                xmax = stod(xmax_, &st);
                ymax = stod(ymax_, &st);

                for (int level = 0; level < nlevels; ++level)
                {
                    vector<cv::KeyPoint>& mkeypoints = mvKeysT[level];  // 提取每一层的金字塔
                    int nkeypointsLevel = (int)mkeypoints.size();
                    if(nkeypointsLevel==0)
                        continue;
                    if (level != 0)
                        scale = mvScaleFactor[level];
                    else
                        scale =1;
                    vector<cv::KeyPoint>::iterator keypoint = mkeypoints.begin();

                    // 标签带有先验动态，则在特征点金字塔内删除该特征点
                    while(keypoint != mkeypoints.end())
                    {
                        //将图像金字塔坐标下的特征点转化为正常尺度下的特征点坐标
                        cv::Point2f search_coord = keypoint->pt * scale;
                        // Search in the semantic image
                        if(search_coord.x >= (imGray.cols -1)) search_coord.x=(imGray.cols -1);
                        if(search_coord.y >= (imGray.rows -1)) search_coord.y=(imGray.rows -1) ;


                        //发现这个特征点的坐标 落在 人 身上，则把这个特征点删除

                        if(search_coord.x >= xmin && search_coord.x <= xmax && search_coord.y >=ymin && search_coord.y <= ymax)
                        {
                            keypoint=mkeypoints.erase(keypoint);	// 将这个特征点删除掉
                        }
                        else
                        {
                            keypoint++;
                        }
                    }
                }

//                cv::rectangle(img, cv::Point(xmin, ymin), cv::Point(xmax, ymax), cv::Scalar(0,0,255), 2);
//                cv::rectangle(mTracker->mImRGB, cv::Point(xmin, ymin), cv::Point(xmax, ymax), cv::Scalar(0,0,255), 2);
            }
            it++;
        }


    }

//    cv::namedWindow("img");
//    cv::imshow("img", img);
//    cv::imwrite("detect_after/"+to_string(mTimeStamp)+".png", img);
//    cv::waitKey(1);

    return flag_orb_mov;
}


} //namespace ORB_SLAM
