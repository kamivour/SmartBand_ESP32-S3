class SensorData {
  final int timestamp;
  final double ax, ay, az;
  final double gx, gy, gz;
  final String label;

  SensorData({
    required this.timestamp,
    required this.ax, required this.ay, required this.az,
    required this.gx, required this.gy, required this.gz,
    required this.label,
  });

  // Chuyển đổi sang List để dùng cho thư viện CSV
  List<dynamic> toCsvRow() {
    return [timestamp, ax, ay, az, gx, gy, gz, label];
  }
}