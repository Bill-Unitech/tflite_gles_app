/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */
#ifndef TFLITE_DETECT_H_
#define TFLITE_DETECT_H_

#include "ssbo_tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_POSE_NUM  10

enum pose_key_id {
    kNose = 0,          //  0
    kLeftEye,           //  1
    kRightEye,          //  2
    kLeftEar,           //  3
    kRightEar,          //  4
    kLeftShoulder,      //  5
    kRightShoulder,     //  6
    kLeftElbow,         //  7
    kRightElbow,        //  8
    kLeftWrist,         //  9
    kRightWrist,        // 10
    kLeftHip,           // 11
    kRightHip,          // 12
    kLeftKnee,          // 13
    kRightKnee,         // 14
    kLeftAnkle,         // 15
    kRightAnkle,        // 16

    kPoseKeyNum
};

typedef struct _pose_key_t
{
    float x;
    float y;
    float score;
} pose_key_t;

typedef struct _pose_t
{
    pose_key_t key[kPoseKeyNum];
    float pose_score;

    void *heatmap;
    int   heatmap_dims[2];  /* heatmap resolution. (9x9) */
} pose_t;

typedef struct _posenet_result_t
{
    int num;
    pose_t pose[MAX_POSE_NUM];
} posenet_result_t;



extern int init_tflite_posenet (int use_quantized_tflite, ssbo_t *ssbo);
extern void  *get_posenet_input_buf (int *w, int *h);

extern int invoke_posenet (posenet_result_t *pose_result);
    
#ifdef __cplusplus
}
#endif

#endif /* TFLITE_DETECT_H_ */
