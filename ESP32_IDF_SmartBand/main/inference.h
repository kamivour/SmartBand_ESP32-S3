#ifndef INFERENCE_H
#define INFERENCE_H

#include <stdint.h>

void model_init(void);
int model_predict(float *input_data, float *conf);
void preprocess_input(float *data, int len);

#endif // INFERENCE_H
