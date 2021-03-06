/***********************************************
  This file is part of the Hand project (https://github.com/libicocco/Hand).

  Copyright(c) 2007-2011 Javier Romero
 * jrgn AT kth DOT se

 Hand is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Hand is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Hand.  If not, see <http://www.gnu.org/licenses/>.

 **********************************************/
#ifndef __CPOSEESTIMATOR_H_
#define __CPOSEESTIMATOR_H_

#include <boost/filesystem.hpp>
#include <queue>
#include <string>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "chandtracker.h"
#include "feature.h"
#include "hog.h"
#include "processFeat.h"
#include "approxNNflann.h"
#include "cTemporalFilter.h"
#include "cPoselistMulti.h"

#include "handclass_config.h"
#include "typeDefinitions.h"


namespace fsystem=boost::filesystem;

enum tReturnCode{OK,notReady,EndOfStream};

struct CGroundTruth
{
    std::queue<tPoseV> mInfoPosesQ;
    tPoseV mInputVar;
    operator bool(){return mInfoPosesQ.empty();}
    void getNextExpectedPose(tPoseV &pExpectedPose){pExpectedPose=mInfoPosesQ.front();mInfoPosesQ.pop();}
    // FIXME:this should be moved out, it's only adding mInputVar
    tPoseV init(tPriorityPathQ &pInfoPathQ) {
        std::vector<tAcc_Var> lPosesVar(NJOINTS+NORI);
        // parse each info file
        while(!pInfoPathQ.empty())
        {              
            fsystem::path lInfoPath=pInfoPathQ.top();
            pInfoPathQ.pop();
            static const unsigned lBufferSz=1024;
            char *lLine=new char[lBufferSz];
            std::fstream lFS(lInfoPath.string(),std::fstream::in|std::fstream::out);
            lFS.getline(lLine,lBufferSz);//comment
            tPoseV lPoseV;
            for(unsigned i=0;i<NORI;++i)
            {
                lFS>>lPoseV[i];
                lPosesVar[i](lPoseV[i]);
            }
            lFS.getline(lLine,lBufferSz);//end ori line
            lFS.getline(lLine,lBufferSz);//comment
            delete []lLine;
            for(unsigned i=NORI;i<NORI+NJOINTS;++i)
            {
                lFS>>lPoseV[i];
                lPosesVar[i](lPoseV[i]);
            }
            mInfoPosesQ.push(lPoseV);
        }
        for(int i=0;i<NJOINTS+NORI;++i)
            mInputVar[i]=acc::variance(lPosesVar[i]);
        return mInputVar;
    }
};

/* for dealing homogeneusly with cameras and files */
struct CCapture
{
    tPriorityPathQ mImPathPQ;
    bool mOffline;
    CGroundTruth mGroundTruth;
    tPoseV mInputVar;

    cv::VideoCapture mCam;

    const tPoseV& getInputVar() const{return mInputVar;}

    tReturnCode getNextFrame(cv::Mat &pIm,tPoseV &pExpectedPose) {
        if(mOffline) { 
            if(mCam.read(pIm))
                return tReturnCode::OK;
            return tReturnCode::EndOfStream;
        }
        else {
            //std::cout << "Reading an image\n";
            mCam >> pIm;
            //std::cout << "Read image in " << pIm.channels() << " channels\n";
            return tReturnCode::OK;
        }
    }

    CCapture(const bool &pOffline, const std::string& pVideoPath) {
        mOffline = pOffline;
        if(!mOffline) {
            mCam.open(0);
        }
        else {
            mCam.open(pVideoPath);
        }
    }
};

template <typename PL,typename tType>
class CPoseEstimator
{
    public:
        CPoseEstimator(
                Feature<tType>* pFeat, ProcessFeat<PL,tType>* pProcFeat,
                const bool &pOffline, const std::string &pVideoPath,
                const bool &pGUI=false):
            mWorsen(INVPROBDEFAULT), 
            mCapture(pOffline,pVideoPath), 
            mFeat(pFeat), 
            mTracker(), 
            mProcFeat(pProcFeat), 
            mOfflineImages(false),
            mGT(false),mGUI(pGUI),mDelay(30),
            mResult(400,640,CV_8UC3),
            mResultName("result") {
                if(mGUI) {
                    cv::namedWindow(mResultName,CV_WINDOW_FREERATIO | CV_WINDOW_AUTOSIZE);
                }
            }

        /** Check if there is a new frame, process it and create images for showing. */
        bool  DoProcessing() {
            cv::Mat lIm;
            tPoseV lExpectedPose(tPoseV::Zero());
            tReturnCode lRet=mCapture.getNextFrame(lIm,lExpectedPose);

            if(lRet==tReturnCode::notReady) {
                return true;
            } else if(lRet==tReturnCode::EndOfStream) {
                return false;
            } else {
                std::pair<cv::Mat,cv::Mat> lImMaskCrop;
                try {
                    cv::Rect lResBox=mTracker.getHand(lIm,lImMaskCrop);
                    //lImMaskCrop.first = lIm;
                    //cv::imshow("first", lImMaskCrop.first);
                    //cv::imshow("second", lImMaskCrop.second);
                    std::vector<tType> lVHog=mFeat->compute(lImMaskCrop,mWorsen);
                    cv::rectangle(lIm, lResBox.tl(), lResBox.br(), cv::Scalar(0, 255, 255)); 
                    mProcFeat->UpdatePoselist(lVHog,lExpectedPose);
                    //std::cout << "error so far: " << (mProcFeat->getMeanErrorV()).sum()/(NORI+NJOINTS) << std::endl;
                    tPoseV lWPose = mProcFeat->getPoselist()->getWPose();

                    std::ofstream outfile;
                    outfile.open("logWPose", std::ios::app);
                    outfile << lWPose << std::endl;
                    outfile.close();

                    if(mGUI)
                    {
                        // construct mResult image
                        buildResultIm(lIm,lImMaskCrop.first);
                        // display it
                        DoExpose();
                    }
                    return true;
                } catch (EError e) {
                    return true;
                }
            }
        }

        float getMeanError() const{return mProcFeat->getMeanError()/(NORI+NJOINTS);}
        float getVarError() const{return mProcFeat->getVarError()/(NORI+NJOINTS);}

        /** If there are images, it shows them */
        void DoExpose()
        {
            cv::imshow(mResultName,mResult);
            // Press Esc to close the program.
            int keyCode = cv::waitKey(mDelay);
            if(keyCode == 27) {
                exit(0);
            } else if (keyCode == 32) {
                cv::waitKey();
            }
        }

        /** Run for non gui interfaces */
        void Run(){while(DoProcessing()){}}

    private:
        void buildResultIm(const cv::Mat &pIm, const cv::Mat &pImCrop)
        {
            const int lPosesPerRow(1), lPosesPerColumn(4);
            const cv::Size lPoseSz(cv::Size(160, 160));
            // 		const boost::ptr_vector<CPose> lPoselist(mProcFeat->getPoselist().getPoselist());
            const CPoselistMulti *lPLM(mProcFeat->getPoselist());
            const boost::ptr_vector<CPose> lPoselist(lPLM->getPoselist());
            
            cv::Rect lImRect(cv::Point(0,0),cv::Size(320,240));
            cv::Mat lResultIm=mResult(lImRect);
            cv::resize(pIm,lResultIm,lImRect.size());
            cv::Rect lImCropRect(cv::Point(320,0),cv::Size(320,240));

            cv::Mat lResultImCrop=mResult(lImCropRect);
            //cv::Mat lImCrop8UC3(pImCrop.size(),CV_8UC3);
            //pImCrop.convertTo(lImCrop8UC3,CV_8UC3);
            //cv::resize(lImCrop8UC3,lResultImCrop,lImCropRect.size()); // could need converting 32FC3 to 8UC3
            lResultImCrop = cv::Scalar(255,255,255);
            mFeat->draw(lResultImCrop);

            for(int i=0;i<lPosesPerColumn;++i)
            {
                for(int j=0;j<lPosesPerRow;++j)
                {
                    if(i*lPosesPerRow+j<lPoselist.size())
                    {
                        cv::Rect lR(cv::Point(i*lPoseSz.width, 240 + j*lPoseSz.height),lPoseSz);
                        cv::Mat lPoseIm(cv::imread(SCENEPATH + lPoselist[i*lPosesPerRow+j].getImagePath().string()));
                        cv::Mat lResultPose=mResult(lR);
                        cv::resize(lPoseIm,lResultPose,lR.size());
                    }
                }
            }
        }


    protected:

        unsigned int mWorsen;
        CCapture mCapture;
        Feature<tType>* mFeat;                   /**< Feature instance*/
        CHandTracker mTracker;      /**< Tracker instance*/
        ProcessFeat<PL,tType>* mProcFeat;/**< ProcessHog instance (includes poselist and nn)*/
        //	cv::Mat mInImage;                 /**< Input Image*/
        //	std::list<fsystem::path> mImPaths;              /**< Image Paths (offline)*/
        std::list<tPoseV> mGTPoses;  /**< Image Paths (offline)*/
        bool mOfflineImages,mGT;    /**< Flags for offline, image ready, step-by-step and ground truth available*/
        const bool mGUI;
        int mDelay;
        cv::Mat mResult;
        const std::string mResultName;
};
#endif
