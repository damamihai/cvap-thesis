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

#ifndef EXACTNN
#define EXACTNN

#include <vector>
#include <math.h>
#include <algorithm>
#include <vector>
#include <fstream>
#include <chrono>
#include <xmmintrin.h>

#include "nn.h"
#include "typeDefinitions.h"

template<typename tType>
class exactNN: public nn<tType>
{
private:
  int mK;
  bool mDataToDeallocate;
  bool mInitialized;
  tType *mDataData;
  tType *mData;
  tType *mFeatDataData;
  const char* mDataPath;
  tPairV mNNdist_all;
  void selfDot(tType* pData, int pNFeat, tType* pSquareData)
  {
    for(int i=0;i<pNFeat;++i)
    {
      tType *lP1=pData+this->pointsDimension*i;
      __m128 lSum=_mm_setzero_ps();
      for(int j=0;j<this->pointsDimension/4;++j)
      {
        __m128 l1=_mm_load_ps(lP1+j*4);
        lSum=_mm_add_ps(lSum,_mm_mul_ps(l1,l1));
      }
      tType lResult[4];
      _mm_storeu_ps(lResult,lSum);        
      pSquareData[i]=lResult[0]+lResult[1]+lResult[2]+lResult[3];
    }
  }
public:
  // pIndexPath is included in order to have a common signature with indexed approxNN like flann
  //exactNN(int pK,int pNPoints=106920,int pDimPoints=512,const char *pDataPath=FLANNBINPATH,const char *pIndexPath="ignored"):
  exactNN(int pK,int pNPoints,int pDimPoints,const char *pDataPath,const char *pIndexPath="ignored"):
  mK(pK),mDataToDeallocate(true),mInitialized(false),mDataPath(pDataPath){this->nn_set_values(pNPoints,pDimPoints,mDataPath);initialize();}
  exactNN(int pK,int pNPoints=106920,int pDimPoints=512,tType *pData=NULL,const char *pIndexPath="ignored"):
  mK(pK),mDataToDeallocate(false),mInitialized(false),mData(pData){this->nn_set_values(pNPoints,pDimPoints,mDataPath);initialize();}
  /**< @todo data.bin and data.idx should be configured with cmake*/
  ~exactNN()
  {
    delete []mDataData;
    delete []mFeatDataData;
    if(mDataToDeallocate)
      delete []mData;
  }
  
  float* getData() const{ return mData;}
  
  bool initialize()
  {
    #ifndef NDEBUG
    namespace sc = std::chrono;
    std::cout << "LOADING DATA FROM " << mDataPath << std::endl;
    auto lSinceEpoch = sc::system_clock::now().time_since_epoch();
    #endif
    if(mDataToDeallocate)
    {
      // where was this set before??????
      mData = new tType[this->nPoints*this->pointsDimension];
      std::ifstream lS(mDataPath,std::ifstream::in|std::ifstream::binary);
      if(lS.fail())
      {
        std::cout << "PROBLEMS OPENING DATA FILE " << mDataPath << " for aNN" << std::endl;
        return 0;
      }
      /*
      for (int i = 0; i < this->nPoints * this->pointsDimension; ++i) {
        lS >> mData[i];
      }*/
      lS.read((char*)mData,this->nPoints*this->pointsDimension*sizeof(tType));
      if (lS) {
         std::cout << "Everything read succesffully\n";
         /*for (int i = 0; i < 512; ++i) {
            printf("%.4f ", mData[i]);
         }*/
      } else {
        std::cout << "Not reading what it should be\n";
      }
    }
    mFeatDataData = new tType[this->nPoints*this->pointsDimension];
    #ifndef NDEBUG
		std::cout << "LOAD TIME: " << 
      sc::duration_cast<sc::microseconds>(lSinceEpoch).count() << " us"  << std::endl;
    #endif
    
    #ifndef NDEBUG
    std::cout << "LOADING INDEX..." << std::endl;
    lSinceEpoch = sc::system_clock::now().time_since_epoch();
    #endif
    mDataData = new tType[this->nPoints*this->pointsDimension];
    selfDot(mData,this->nPoints,mDataData);
    mNNdist_all.reserve(this->nPoints);
    mNNdist_all.resize(this->nPoints);
    
    #ifndef NDEBUG
		std::cout << "LOAD TIME: " << 
      sc::duration_cast<sc::microseconds>(lSinceEpoch).count() << " us"  << std::endl;
    #endif
    //        std::cout << "INDEX COULDN'T BE LOADED" << std::endl;
    //        return 0;
    return true;
  }// i had to do it in the constructor
  const tPairV& computeNN(const std::vector<tType>& pFeat)
  {
    #ifndef NDEBUG
    namespace sc = std::chrono;
    auto lSinceEpoch = sc::system_clock::now().time_since_epoch();
    #endif
    tType lFeatData[this->pointsDimension];
    //     std::cout << this->pointsDimension << std::endl;
    if(pFeat.size()!=this->pointsDimension)
    {
      std::cout << "Feature size " << pFeat.size() << " doesn't match db features size " << this->pointsDimension << std::endl;
      exit(-1);
    }
    std::copy(pFeat.begin(),pFeat.end(),lFeatData);
    // all mFeatDataData is innecessary, only makes dist(a,a)=0
    selfDot(lFeatData,1,mFeatDataData);
    for(int i=0;i<this->nPoints;i++)
    {
      tType *lP1=mData+this->pointsDimension*i;
      tType *lP2=lFeatData;
      
      __m128 lSum=_mm_setzero_ps();
      for(int j=0;j<this->pointsDimension/4;j++)
      {
        __m128 l1=_mm_load_ps(lP1+j*4);
        __m128 l2=_mm_load_ps(lP2+j*4);
        lSum=_mm_add_ps(lSum,_mm_mul_ps(l1,l2));
      }
      
      tType lResult[4];
      _mm_storeu_ps(lResult,lSum);        
      //        tType lDist = mDataData[i]-2*(lResult[0]+lResult[1]+lResult[2]+lResult[3]);
      float lDist = mFeatDataData[0]+mDataData[i]-2*(lResult[0]+lResult[1]+lResult[2]+lResult[3]);
      //                 std::cout <<  i << ": " << lDist << "; " << mFeatDataData[0] << " " << mDataData[i] << " " << 2*(lResult[0]+lResult[1]+lResult[2]+lResult[3]) << ";" << lResult[0] << " " << lResult[1] << " " << lResult[2] << " " << lResult[3] << std::endl;
      if(fabs(lDist)<0.000001)
        lDist = 0.0;
      if(isnan(sqrt(lDist)))
      {
        std::cerr << "ERROR in vector " << i << " : sqrt(lDist)=NAN ; lDist= " << lDist << std::endl;
        std::cerr << mFeatDataData[0] << " " << mDataData[i] << " " << lResult[0] << " " << lResult[1] << " " << lResult[2] << " " << lResult[3] << std::endl;
        exit(-1);
      }
      tUnsDouble lPair = tUnsDouble(i,sqrt(lDist));
      //std::cout << lPair.first << "," << lPair.second << std::endl;
      mNNdist_all[i]= lPair;
      //lR[i]=std::pair<int,float>(i,mDataData[i]-2*(lResult[0]+lResult[1]+lResult[2]+lResult[3]));
    }
    std::sort(mNNdist_all.begin(),mNNdist_all.end(),comppair);
    this->nndist.reserve(mK);
    this->nndist.resize(mK);
    std::copy(mNNdist_all.begin(),mNNdist_all.begin()+mK,this->nndist.begin());
    //    std::vector<std::pair<int,float> >::iterator lIt;
    //    for(lIt=this->nndist.begin();lIt!=this->nndist.end();++lIt)
    //        std::cout << "( " << lIt->first << "," << lIt->second << ")\t";
    //    std::cout << std::endl;
    return this->nndist;
  }
};

#endif
