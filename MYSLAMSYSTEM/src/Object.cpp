//
// Created by zhu on 2023/4/26.
//

#include <Object.h>
#include "vector"
#include "math.h"
#define PI 3.14159265

namespace ORB_SLAM3 {
    Object::Object() {
        bdynamic_ = false;      //初始化目标点状态
        vdetect_parameter = {0,0,0,0};
        ndetect_class = -1;
    }

    Object::Object(vector<double> vdetect_parameter_,int ndetect_class_):
            vdetect_parameter(vdetect_parameter_),ndetect_class(ndetect_class_){}

    Object::~Object(){}

    vector<double> Object::GetDetectParameter(){
        return vdetect_parameter;
    }

    int Object::GetDetectClass(){
        return ndetect_class;
    }

}