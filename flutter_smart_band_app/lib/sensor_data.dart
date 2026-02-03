class SensorData {
  final int timestamp;
  final double pitch, roll, svm;
  final double gx, gy, gz;
  final int hr, spo2;
  final String label;
  final bool isFallDetected; // NEW: Fall detection from AI model

  SensorData({
    required this.timestamp,
    required this.pitch,
    required this.roll,
    required this.svm,
    required this.gx,
    required this.gy,
    required this.gz,
    this.hr = 0,
    this.spo2 = 0,
    required this.label,
    this.isFallDetected = false,
  });

  List<dynamic> toCsvRow() {
    return [timestamp, pitch, roll, svm, gx, gy, gz, hr, spo2, label, isFallDetected ? 1 : 0];
  }
}
