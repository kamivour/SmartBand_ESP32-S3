#include "kalman_filter.h"
#include <math.h>

void Kalman_Init(Kalman_t *k) {
    k->Q_angle = 0.001;
    k->Q_bias = 0.003;
    k->R_measure = 0.3;  // Increased from 0.03 to reduce accelerometer noise
    k->angle = 0.0;
    k->bias = 0.0;
    k->P[0][0] = 0.0;
    k->P[0][1] = 0.0;
    k->P[1][0] = 0.0;
    k->P[1][1] = 0.0;
}

double Kalman_GetAngle(Kalman_t *k, double newAngle, double newRate, double dt) {
    // Predict
    double rate = newRate - k->bias;
    k->angle += rate * dt;
    
    // Update error covariance
    k->P[0][0] += dt * (dt * k->P[1][1] - k->P[0][1] - k->P[1][0] + k->Q_angle);
    k->P[0][1] -= dt * k->P[1][1];
    k->P[1][0] -= dt * k->P[1][1];
    k->P[1][1] += k->Q_bias * dt;
    
    // Update
    double S = k->P[0][0] + k->R_measure;
    double K[2];
    K[0] = k->P[0][0] / S;
    K[1] = k->P[1][0] / S;
    
    double y = newAngle - k->angle;
    k->angle += K[0] * y;
    k->bias += K[1] * y;
    
    double P00_temp = k->P[0][0];
    double P01_temp = k->P[0][1];
    k->P[0][0] -= K[0] * P00_temp;
    k->P[0][1] -= K[0] * P01_temp;
    k->P[1][0] -= K[1] * P00_temp;
    k->P[1][1] -= K[1] * P01_temp;
    
    return k->angle;
}

void SimpleKalman_Init(SimpleKalman_t *k, double mea_e, double est_e, double q) {
    k->err_measure = mea_e;
    k->err_estimate = est_e;
    k->q = q;
    k->current_estimate = 0;
    k->last_estimate = 0;
}

double SimpleKalman_Update(SimpleKalman_t *k, double mea) {
    k->kalman_gain = k->err_estimate / (k->err_estimate + k->err_measure);
    k->current_estimate = k->last_estimate + k->kalman_gain * (mea - k->last_estimate);
    k->err_estimate = (1.0 - k->kalman_gain) * k->err_estimate + 
                      k->q * fabs(k->last_estimate - k->current_estimate);
    k->last_estimate = k->current_estimate;
    return k->current_estimate;
}
