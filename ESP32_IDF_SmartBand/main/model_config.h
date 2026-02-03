#ifndef MODEL_CONFIG_H
#define MODEL_CONFIG_H

#include <stdint.h>

// Window Duration: 2.0s
#define MODEL_WINDOW_SIZE 100
#define MODEL_NUM_FEATURES 6
#define MODEL_NUM_CLASSES 2

// Normalization Parameters
static const float MODEL_MEAN[6] = {-11.942094, -22.315664, 1.1542875,
                                    -0.8585043, 1.0363606,  -0.3485452};
static const float MODEL_STD[6] = {38.571507, 61.8041,  0.40603083,
                                   77.61616,  77.04766, 100.30823};

#endif
