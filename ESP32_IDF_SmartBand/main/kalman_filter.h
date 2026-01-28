#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

// Kalman filter for angle fusion (accelerometer + gyroscope)
typedef struct {
    double Q_angle;    // Process noise covariance for angle
    double Q_bias;     // Process noise covariance for bias
    double R_measure;  // Measurement noise covariance
    double angle;      // Estimated angle
    double bias;       // Estimated gyro bias
    double P[2][2];    // Error covariance matrix
} Kalman_t;

// Simple Kalman filter for 1D signal smoothing
typedef struct {
    double err_measure;      // Measurement error
    double err_estimate;     // Estimation error
    double q;                // Process noise
    double current_estimate; // Current estimate
    double last_estimate;    // Last estimate
    double kalman_gain;      // Kalman gain
} SimpleKalman_t;

/**
 * @brief Initialize Kalman filter for angle fusion
 * 
 * @param k Pointer to Kalman_t structure
 */
void Kalman_Init(Kalman_t *k);

/**
 * @brief Update Kalman filter with new measurements
 * 
 * @param k Pointer to Kalman_t structure
 * @param newAngle New angle from accelerometer (degrees)
 * @param newRate New angular rate from gyroscope (deg/s)
 * @param dt Time step (seconds)
 * @return Filtered angle (degrees)
 */
double Kalman_GetAngle(Kalman_t *k, double newAngle, double newRate, double dt);

/**
 * @brief Initialize simple Kalman filter
 * 
 * @param k Pointer to SimpleKalman_t structure
 * @param mea_e Measurement error
 * @param est_e Initial estimation error
 * @param q Process noise
 */
void SimpleKalman_Init(SimpleKalman_t *k, double mea_e, double est_e, double q);

/**
 * @brief Update simple Kalman filter
 * 
 * @param k Pointer to SimpleKalman_t structure
 * @param mea New measurement
 * @return Filtered value
 */
double SimpleKalman_Update(SimpleKalman_t *k, double mea);

#endif // KALMAN_FILTER_H
