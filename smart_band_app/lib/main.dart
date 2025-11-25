import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:csv/csv.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:intl/intl.dart';
import 'package:path_provider/path_provider.dart';

import 'sensor_data.dart';

void main() {
  runApp(const SmartBandApp());
}

class SmartBandApp extends StatelessWidget {
  const SmartBandApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'AI Data Collector',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
        useMaterial3: true,
        textTheme: GoogleFonts.robotoTextTheme(),
      ),
      home: const DashboardScreen(),
    );
  }
}

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  // --- CONNECTION STATE ---
  RawDatagramSocket? _udpSocket;
  bool _isConnected = false;
  String _statusText = "Disconnected";

  // --- DATA STATE ---
  // Data hiển thị tức thời (cho text box)
  Map<String, dynamic> _latestData = {
    "ts": 0, "bat": 0, "ax": 0.0, "ay": 0.0, "az": 0.0
  };

  // Buffer cho biểu đồ (giữ 50-100 điểm dữ liệu gần nhất để vẽ)
  final List<FlSpot> _spotsAx = [];
  final List<FlSpot> _spotsAy = [];
  final List<FlSpot> _spotsAz = [];
  double _chartX = 0; // Trục X giả lập cho biểu đồ trôi

  // --- RECORDING STATE ---
  bool _isRecording = false;
 final List<SensorData> _recordedBuffer = []; // RAM Buffer để lưu file CSV
  
  // Dropdown Label
  final List<String> _labels = ["Normal", "Walking", "Running", "Falling", "Lying Down"];
  String _selectedLabel = "Normal";

  Timer? _uiTimer;

  @override
  void initState() {
    super.initState();
    // Throttle: Update UI mỗi 100ms (10 FPS)
    _uiTimer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (mounted && _isConnected) {
        setState(() {
          // Trigger vẽ lại biểu đồ và cập nhật số liệu
        });
      }
    });
  }

  @override
  void dispose() {
    _uiTimer?.cancel();
    _stopUdp();
    super.dispose();
  }

  // --- UDP LOGIC ---
  Future<void> _startUdp() async {
    try {
      _stopUdp();
      _udpSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 4210);
      setState(() {
        _isConnected = true;
        _statusText = "Listening on Port 4210...";
      });

      _udpSocket!.listen((RawSocketEvent event) {
        if (event == RawSocketEvent.read) {
          Datagram? dg = _udpSocket!.receive();
          if (dg != null) {
            String msg = utf8.decode(dg.data);
            _processPacket(msg);
          }
        }
      });
    } catch (e) {
      setState(() => _statusText = "Error: $e");
    }
  }

  void _stopUdp() {
    _udpSocket?.close();
    _udpSocket = null;
    setState(() {
      _isConnected = false;
      _statusText = "Disconnected";
    });
  }

  void _processPacket(String jsonStr) {
    try {
      final data = jsonDecode(jsonStr);
      
      // 1. Cập nhật biến hiển thị số
      _latestData = data;

      // 2. Cập nhật dữ liệu cho biểu đồ (Chart Buffer)
      // Chỉ giữ lại 100 điểm để chart không bị lag
      if (_spotsAx.length > 100) {
        _spotsAx.removeAt(0);
        _spotsAy.removeAt(0);
        _spotsAz.removeAt(0);
      }
      _chartX++; // Tăng trục thời gian ảo
      _spotsAx.add(FlSpot(_chartX, (data['ax'] as num).toDouble()));
      _spotsAy.add(FlSpot(_chartX, (data['ay'] as num).toDouble()));
      _spotsAz.add(FlSpot(_chartX, (data['az'] as num).toDouble()));

      // 3. Nếu đang Recording -> Lưu vào RAM Buffer
      if (_isRecording) {
        _recordedBuffer.add(SensorData(
          timestamp: data['ts'],
          ax: (data['ax'] as num).toDouble(),
          ay: (data['ay'] as num).toDouble(),
          az: (data['az'] as num).toDouble(),
          gx: (data['gx'] as num).toDouble(),
          gy: (data['gy'] as num).toDouble(),
          gz: (data['gz'] as num).toDouble(),
          label: _selectedLabel,
        ));
      }

    } catch (e) {
      debugPrint("Parse Error: $e");
    }
  }

  // --- CSV EXPORT LOGIC ---
  Future<void> _toggleRecording() async {
    if (_isRecording) {
      // Đang ghi -> Bấm dừng -> Xuất file
      setState(() => _isRecording = false);
      await _exportToCsv();
    } else {
      // Đang dừng -> Bấm ghi -> Xóa buffer cũ, bắt đầu ghi mới
      setState(() {
        _recordedBuffer.clear();
        _isRecording = true;
      });
    }
  }

  Future<void> _exportToCsv() async {
    if (_recordedBuffer.isEmpty) {
      _showSnack("No data to export!");
      return;
    }

    // 1. Convert Data sang CSV String
    List<List<dynamic>> rows = [];
    // Header
    rows.add(["timestamp", "acc_x", "acc_y", "acc_z", "gyro_x", "gyro_y", "gyro_z", "label"]);
    // Data
    for (var d in _recordedBuffer) {
      rows.add(d.toCsvRow());
    }
    String csvData = const ListToCsvConverter().convert(rows);

    try {
      // 2. Lấy đường dẫn lưu file (Android/data/com.example.../files)
      // Không cần quyền MANAGE_EXTERNAL_STORAGE phức tạp
      final directory = await getExternalStorageDirectory();
      if (directory == null) return;

      String timestamp = DateFormat('yyyyMMdd_HHmmss').format(DateTime.now());
      String fileName = "sensor_${_selectedLabel}_$timestamp.csv";
      String path = "${directory.path}/$fileName";

      final file = File(path);
      await file.writeAsString(csvData);

      _showSnack("Saved to: $path");
      debugPrint("File saved at: $path");
      
    } catch (e) {
      _showSnack("Error saving file: $e");
    }
  }

  void _showSnack(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  // --- UI WIDGETS ---

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("AI Data Collector"),
        backgroundColor: Colors.indigo,
        foregroundColor: Colors.white,
        actions: [
          Center(
            child: Padding(
              padding: const EdgeInsets.only(right: 15),
              child: Text("BAT: ${_latestData['bat']}%", style: const TextStyle(fontWeight: FontWeight.bold)),
            ),
          )
        ],
      ),
      body: Column(
        children: [
          // 1. LIVE CHART SECTION
          Container(
            height: 250,
            padding: const EdgeInsets.all(10),
            color: Colors.black87,
            child: LineChart(
              LineChartData(
                gridData: const FlGridData(show: false),
                titlesData: const FlTitlesData(show: false),
                borderData: FlBorderData(show: true, border: Border.all(color: Colors.white12)),
                minY: -15, maxY: 15, // Giới hạn trục Y (tùy chỉnh theo G-force)
                lineBarsData: [
                  _buildLine(_spotsAx, Colors.red),   // X = Đỏ
                  _buildLine(_spotsAy, Colors.green), // Y = Xanh lá
                  _buildLine(_spotsAz, Colors.blue),  // Z = Xanh dương
                ],
              ),
            ),
          ),
          
          // 2. INFO & CONTROL
          Expanded(
            child: Container(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  // Status & Data Text
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text("Status: $_statusText", style: const TextStyle(fontSize: 12, color: Colors.grey)),
                      Text("Buffer: ${_recordedBuffer.length} samples", 
                        style: TextStyle(color: _isRecording ? Colors.red : Colors.grey, fontWeight: FontWeight.bold)),
                    ],
                  ),
                  const SizedBox(height: 10),
                  
                  // Label Dropdown
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 12),
                    decoration: BoxDecoration(
                      border: Border.all(color: Colors.grey),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: DropdownButtonHideUnderline(
                      child: DropdownButton<String>(
                        isExpanded: true,
                        value: _selectedLabel,
                        items: _labels.map((e) => DropdownMenuItem(value: e, child: Text(e))).toList(),
                        onChanged: _isRecording ? null : (v) => setState(() => _selectedLabel = v!),
                      ),
                    ),
                  ),
                  const Spacer(),

                  // Buttons Row
                  Row(
                    children: [
                      // Connect Button
                      Expanded(
                        child: ElevatedButton.icon(
                          onPressed: _isConnected ? null : _startUdp,
                          icon: const Icon(Icons.wifi),
                          label: const Text("CONNECT"),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: Colors.indigo,
                            foregroundColor: Colors.white,
                            padding: const EdgeInsets.symmetric(vertical: 16),
                          ),
                        ),
                      ),
                      const SizedBox(width: 10),
                      // Disconnect Button
                      IconButton.filledTonal(
                         onPressed: _isConnected ? _stopUdp : null,
                         icon: const Icon(Icons.power_settings_new),
                         color: Colors.red,
                      )
                    ],
                  ),
                  const SizedBox(height: 10),

                  // Record / Stop & Export Button
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton.icon(
                      onPressed: _isConnected ? _toggleRecording : null,
                      icon: Icon(_isRecording ? Icons.stop : Icons.fiber_manual_record),
                      label: Text(_isRecording ? "STOP & EXPORT CSV" : "START RECORDING"),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: _isRecording ? Colors.red : Colors.green,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 20),
                        textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  LineChartBarData _buildLine(List<FlSpot> spots, Color color) {
    return LineChartBarData(
      spots: spots,
      isCurved: true, // Làm mượt đường cong
      color: color,
      barWidth: 2,
      dotData: const FlDotData(show: false), // Ẩn các dấu chấm
    );
  }
}