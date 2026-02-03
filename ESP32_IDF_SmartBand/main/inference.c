#include "inference.h"
#include "model_config.h"
#include "model_weights.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SIMD Detection for ESP32-S3
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define USE_ESP32S3_SIMD 1
// ESP32-S3 has Xtensa LX7 with vector extensions
// We'll use optimized implementations when available
#else
#define USE_ESP32S3_SIMD 0
#endif

// Helper for Softmax (Standard implementation to avoid external dependencies)
static void custom_softmax(float *data, int len) {
  float max = data[0];
  for (int i = 1; i < len; i++)
    if (data[i] > max)
      max = data[i];

  float sum = 0;
  for (int i = 0; i < len; i++) {
    data[i] = expf(data[i] - max); // Stable softmax
    sum += data[i];
  }
  for (int i = 0; i < len; i++)
    data[i] /= sum;
}

// =================================================================================
// LAYER DEFINITIONS (Generic C implementation compatible with ESP32)
// =================================================================================

// Dequantize Helper: int8_t weights -> float buffer
void pcm_dequantize_weights(const int8_t *src, float *dst, int len,
                            float scale) {
  for (int i = 0; i < len; i++) {
    dst[i] = src[i] * scale;
  }
}

// Dequantize Bias: int32_t -> float
void pcm_dequantize_bias(const int32_t *src, float *dst, int len, float scale) {
  for (int i = 0; i < len; i++) {
    dst[i] = (float)src[i]; // Cast to float first if needed, but here assuming
                            // src[i] is int value
    dst[i] = src[i] * scale;
  }
}

// ---------------------------
// CONV 1D LAYER (SIMD OPTIMIZED)
// ---------------------------
#if USE_ESP32S3_SIMD
void layer_conv1d_simd(const float *input,   // Shape: [Len_in * Ch_in]
                       float *output,        // Shape: [Len_out * Ch_out]
                       const float *weights, // Shape: [K * Ch_in * Ch_out]
                       const float *bias,    // Shape: [Ch_out]
                       int len_in, int ch_in, int len_out, int ch_out,
                       int kernel_size, int stride) {
  // SIMD optimization: Process multiple output channels simultaneously
  // ESP32-S3 can do 4 float operations in parallel

  for (int t = 0; t < len_out; t++) {
    int t_in_start = t * stride;

    // Process 4 output channels at a time (SIMD-friendly)
    int co_vec = 0;
    for (; co_vec + 3 < ch_out; co_vec += 4) {
      float sum[4] = {bias[co_vec], bias[co_vec + 1], bias[co_vec + 2],
                      bias[co_vec + 3]};

      // Inner loops - compiler can auto-vectorize this
      for (int k = 0; k < kernel_size; k++) {
        int t_in = t_in_start + k;
        for (int ci = 0; ci < ch_in; ci++) {
          int i_idx = t_in * ch_in + ci;
          float in_val = input[i_idx];

          // Process 4 weights at once
          for (int v = 0; v < 4; v++) {
            int w_idx = (k * ch_in + ci) * ch_out + (co_vec + v);
            sum[v] += in_val * weights[w_idx];
          }
        }
      }

      // ReLU activation
      for (int v = 0; v < 4; v++) {
        output[t * ch_out + co_vec + v] = (sum[v] > 0) ? sum[v] : 0;
      }
    }

    // Handle remaining channels (scalar fallback)
    for (int co = co_vec; co < ch_out; co++) {
      float sum = bias[co];
      for (int k = 0; k < kernel_size; k++) {
        int t_in = t_in_start + k;
        for (int ci = 0; ci < ch_in; ci++) {
          int w_idx = (k * ch_in + ci) * ch_out + co;
          int i_idx = t_in * ch_in + ci;
          sum += input[i_idx] * weights[w_idx];
        }
      }
      output[t * ch_out + co] = (sum > 0) ? sum : 0;
    }
  }
}
#endif

// ---------------------------
// CONV 1D LAYER
// ---------------------------
void layer_conv1d(const float *input,   // Shape: [Len_in * Ch_in]
                  float *output,        // Shape: [Len_out * Ch_out]
                  const float *weights, // Shape: [K * Ch_in * Ch_out]
                  const float *bias,    // Shape: [Ch_out]
                  int len_in, int ch_in, int len_out, int ch_out,
                  int kernel_size, int stride) {
#if USE_ESP32S3_SIMD
  // Use SIMD-optimized version on ESP32-S3
  layer_conv1d_simd(input, output, weights, bias, len_in, ch_in, len_out,
                    ch_out, kernel_size, stride);
#else
  // Original scalar implementation
  // For each output time step
  for (int t = 0; t < len_out; t++) {
    int t_in_start = t * stride;

    // For each output channel (filter)
    for (int co = 0; co < ch_out; co++) {
      float sum = bias[co];

      // Convolve: Loop over kernel size
      for (int k = 0; k < kernel_size; k++) {
        int t_in = t_in_start + k;
        for (int ci = 0; ci < ch_in; ci++) {
          int w_idx = (k * ch_in + ci) * ch_out + co;
          int i_idx = t_in * ch_in + ci;
          sum += input[i_idx] * weights[w_idx];
        }
      }

      // Activation (ReLU)
      if (sum < 0)
        sum = 0;
      output[t * ch_out + co] = sum;
    }
  }
#endif
}

// ---------------------------
// MAX POOL 1D
// ---------------------------
void layer_maxpool1d(const float *input, float *output, int len_in, int ch_in,
                     int pool_size, int stride) {
  int len_out = (len_in - pool_size) / stride + 1;

  for (int t = 0; t < len_out; t++) {
    for (int c = 0; c < ch_in; c++) {
      float max_val = -1e9;
      for (int k = 0; k < pool_size; k++) {
        int t_in = t * stride + k;
        float val = input[t_in * ch_in + c];
        if (val > max_val)
          max_val = val;
      }
      output[t * ch_in + c] = max_val;
    }
  }
}

// ---------------------------
// GLOBAL AVERAGE POOLING
// ---------------------------
void layer_global_avg_pool(const float *input, float *output, int len_in,
                           int ch_in) {
  for (int c = 0; c < ch_in; c++) {
    float sum = 0;
    for (int t = 0; t < len_in; t++) {
      sum += input[t * ch_in + c];
    }
    output[c] = sum / len_in;
  }
}

// ---------------------------
// DENSE LAYER (SIMD OPTIMIZED)
// ---------------------------
#if USE_ESP32S3_SIMD
void layer_dense_simd(const float *input, float *output,
                      const float *weights, // [In, Out]
                      const float *bias,    // [Out]
                      int in_dim, int out_dim, int use_relu) {
  // Process 4 output neurons at once
  int o_vec = 0;
  for (; o_vec + 3 < out_dim; o_vec += 4) {
    float sum[4] = {bias[o_vec], bias[o_vec + 1], bias[o_vec + 2],
                    bias[o_vec + 3]};

    // Dot product for 4 outputs simultaneously
    for (int i = 0; i < in_dim; i++) {
      float in_val = input[i];
      // Multiply with 4 weight values
      for (int v = 0; v < 4; v++) {
        sum[v] += in_val * weights[i * out_dim + o_vec + v];
      }
    }

    // Apply ReLU if needed
    for (int v = 0; v < 4; v++) {
      if (use_relu && sum[v] < 0)
        sum[v] = 0;
      output[o_vec + v] = sum[v];
    }
  }

  // Handle remaining outputs (scalar)
  for (int o = o_vec; o < out_dim; o++) {
    float sum = bias[o];
    for (int i = 0; i < in_dim; i++) {
      sum += input[i] * weights[i * out_dim + o];
    }
    if (use_relu && sum < 0)
      sum = 0;
    output[o] = sum;
  }
}
#endif

// ---------------------------
// DENSE LAYER
// ---------------------------
void layer_dense(const float *input, float *output,
                 const float *weights, // [In, Out]
                 const float *bias,    // [Out]
                 int in_dim, int out_dim, int use_relu) {
#if USE_ESP32S3_SIMD
  // Use SIMD-optimized version on ESP32-S3
  layer_dense_simd(input, output, weights, bias, in_dim, out_dim, use_relu);
#else
  // Original scalar implementation
  for (int o = 0; o < out_dim; o++) {
    float sum = bias[o];
    for (int i = 0; i < in_dim; i++) {
      sum += input[i] * weights[i * out_dim + o];
    }

    if (use_relu && sum < 0)
      sum = 0;
    output[o] = sum;
  }
#endif
}

// =================================================================================
// MODEL INSTANCE & BUFFERS
// =================================================================================

#define C1_K 3
#define C1_CIN 6
#define C1_COUT 16

#define C2_K 3
#define C2_CIN 16
#define C2_COUT 32

#define D1_IN 32
#define D1_OUT 32

#define D2_IN 32
#define D2_OUT 2

// Weight Buffers
static float w_conv1[C1_K * C1_CIN * C1_COUT];
static float b_conv1[C1_COUT];

static float w_conv2[C2_K * C2_CIN * C2_COUT];
static float b_conv2[C2_COUT];

static float w_dense1[D1_IN * D1_OUT];
static float b_dense1[D1_OUT];

static float w_dense2[D2_IN * D2_OUT];
static float b_dense2[D2_OUT];

void model_init() {
  printf("Initializing Model... Dequantizing weights...\n");

#if USE_ESP32S3_SIMD
  printf("SIMD Optimization: ENABLED (ESP32-S3 Xtensa LX7)\n");
#else
  printf("SIMD Optimization: DISABLED (Scalar mode)\n");
#endif

  pcm_dequantize_weights(CONV1_W, w_conv1, sizeof(CONV1_W), CONV1_W_SCALE);
  pcm_dequantize_bias(CONV1_B, b_conv1, sizeof(CONV1_B) / 4, CONV1_W_SCALE);

  pcm_dequantize_weights(CONV2_W, w_conv2, sizeof(CONV2_W), CONV2_W_SCALE);
  pcm_dequantize_bias(CONV2_B, b_conv2, sizeof(CONV2_B) / 4, CONV2_W_SCALE);

  pcm_dequantize_weights(DENSE1_W, w_dense1, sizeof(DENSE1_W), DENSE1_W_SCALE);
  pcm_dequantize_bias(DENSE1_B, b_dense1, sizeof(DENSE1_B) / 4, DENSE1_W_SCALE);

  pcm_dequantize_weights(DENSE2_W, w_dense2, sizeof(DENSE2_W), DENSE2_W_SCALE);
  pcm_dequantize_bias(DENSE2_B, b_dense2, sizeof(DENSE2_B) / 4, DENSE2_W_SCALE);
  printf("Model Ready!\n");
}

// =================================================================================
// INFERENCE
// =================================================================================

// Intermediate buffers (aligned for SIMD on ESP32-S3)
#if USE_ESP32S3_SIMD
__attribute__((aligned(16))) static float buf_conv1[98 * 16];
__attribute__((aligned(16))) static float buf_pool1[49 * 16];
__attribute__((aligned(16))) static float buf_conv2[47 * 32];
__attribute__((aligned(16))) static float buf_gap[32];
__attribute__((aligned(16))) static float buf_dense1[32];
__attribute__((aligned(16))) static float buf_dense2[2];
#else
static float buf_conv1[98 * 16];
static float buf_pool1[49 * 16];
static float buf_conv2[47 * 32];
static float buf_gap[32];
static float buf_dense1[32];
static float buf_dense2[2];
#endif

int model_predict(float *input_data, float *conf) {
  // 1. Conv1
  layer_conv1d(input_data, buf_conv1, w_conv1, b_conv1, 100, 6, 98, 16, 3, 1);

  // 2. MaxPool1
  layer_maxpool1d(buf_conv1, buf_pool1, 98, 16, 2, 2);

  // 3. Conv2
  layer_conv1d(buf_pool1, buf_conv2, w_conv2, b_conv2, 49, 16, 47, 32, 3, 1);

  // 4. GAP
  layer_global_avg_pool(buf_conv2, buf_gap, 47, 32);

  // 5. Dense1
  layer_dense(buf_gap, buf_dense1, w_dense1, b_dense1, 32, 32, 1);

  // 6. Dense2 + Softmax
  layer_dense(buf_dense1, buf_dense2, w_dense2, b_dense2, 32, 2, 0);
  custom_softmax(buf_dense2, 2);

  if (conf)
    *conf = buf_dense2[1];

  if (buf_dense2[1] > 0.5)
    return 1; // Fall
  return 0;   // Normal
}

// Data Preprocessor
void preprocess_input(float *data, int len) {
  // Standardize using exported Mean/Std
  // Assuming input data is [100, 6] flat
  for (int i = 0; i < len; i++) {
    int feat_idx = i % 6;
    data[i] = (data[i] - MODEL_MEAN[feat_idx]) / MODEL_STD[feat_idx];
  }
}
