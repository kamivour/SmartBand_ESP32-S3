class SensorData {
  final int timestamp;
  final double pitch, roll, svm;
  final double gx, gy, gz;
  final String label;

  SensorData({
    required this.timestamp,
    required this.pitch,
    required this.roll,
    required this.svm,
    required this.gx,
    required this.gy,
    required this.gz,
    required this.label,
  });

  List<dynamic> toCsvRow() {
    return [timestamp, pitch, roll, svm, gx, gy, gz, label];
  }
}
