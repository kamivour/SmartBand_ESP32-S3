import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:universal_io/io.dart';

import 'package:flutter/foundation.dart';
import 'package:csv/csv.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';

import 'sensor_data.dart';

void main() => runApp(const SmartBandApp());

class SmartBandApp extends StatelessWidget {
  const SmartBandApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Band Monitor',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.teal),
        useMaterial3: true,
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
  // ESP32 BLE Configuration (from ESP-IDF NimBLE)
  static const String _esp32DeviceName = 'ESP32 SmartBand';
  static const String _serviceUuid = '4fafc201-1fb5-459e-8fcc-c5c9c333914b';
  static const String _characteristicUuid = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
  
  // Connection
  RawDatagramSocket? _udpSocket;
  BluetoothDevice? _bleDevice;
  StreamSubscription<List<int>>? _bleSub;
  bool _isConnected = false;
  String _connType = 'NONE';
  String _status = 'Disconnected';
  Timer? _simTimer;

  // Data
  Map<String, dynamic> _data = {
    'ts': 0, 'bat': 0, 
    'pitch': 0.0, 'roll': 0.0, 'svm': 0.0, 
    'gx': 0.0, 'gy': 0.0, 'gz': 0.0,
    'hr': 0, 'spo2': 0,
  };
  
  // Chart data
  final List<FlSpot> _pitchSpots = [], _rollSpots = [], _svmSpots = [];
  final List<FlSpot> _gxSpots = [], _gySpots = [], _gzSpots = [];
  double _x = 0;
  
  // UI state
  bool _showGraph = true;
  
  // Recording
  bool _isRecording = false;
  final List<SensorData> _buffer = [];
  String _label = 'Normal';
  static const _labels = ['Normal', 'Walking', 'Running', 'Falling', 'Lying Down'];
  
  // Fall Detection
  bool _fallDetected = false;
  Timer? _flashTimer;
  bool _flashState = false;

  @override
  void dispose() {
    _disconnect();
    _flashTimer?.cancel();
    super.dispose();
  }

  void _onData(String json) {
    try {
      final d = jsonDecode(json);
      _data = d;
      
      // Update chart (keep last 100 points)
      if (_pitchSpots.length > 100) {
        _pitchSpots.removeAt(0); _rollSpots.removeAt(0); _svmSpots.removeAt(0);
        _gxSpots.removeAt(0); _gySpots.removeAt(0); _gzSpots.removeAt(0);
      }
      _x++;
      _pitchSpots.add(FlSpot(_x, (d['pitch'] as num).toDouble()));
      _rollSpots.add(FlSpot(_x, (d['roll'] as num).toDouble()));
      _svmSpots.add(FlSpot(_x, (d['svm'] as num).toDouble()));
      _gxSpots.add(FlSpot(_x, (d['gx'] as num).toDouble()));
      _gySpots.add(FlSpot(_x, (d['gy'] as num).toDouble()));
      _gzSpots.add(FlSpot(_x, (d['gz'] as num).toDouble()));
      
      // Check for fall detection
      final fallStatus = (d['fall'] ?? 0);
      if (fallStatus == 1 && !_fallDetected) {
        _triggerFallAlert();
      }
      
      if (_isRecording) {
        _buffer.add(SensorData(
          timestamp: d['ts'], pitch: d['pitch'].toDouble(), roll: d['roll'].toDouble(),
          svm: d['svm'].toDouble(), gx: d['gx'].toDouble(), gy: d['gy'].toDouble(), 
          gz: d['gz'].toDouble(), 
          hr: (d['hr'] ?? 0).toInt(), spo2: (d['spo2'] ?? 0).toInt(),
          label: _label,
          isFallDetected: fallStatus == 1,
        ));
      }
      
      setState(() {});
    } catch (_) {}
  }

  // WiFi UDP
  Future<void> _startUdp() async {
    await _disconnect();
    try {
      _udpSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 4210);
      setState(() { _isConnected = true; _connType = 'WIFI'; _status = 'WiFi: Port 4210'; });
      _udpSocket!.listen((e) {
        if (e == RawSocketEvent.read) {
          final dg = _udpSocket!.receive();
          if (dg != null) _onData(utf8.decode(dg.data));
        }
      });
    } catch (e) {
      setState(() => _status = 'WiFi Error: $e');
    }
  }

  // Check if location services are enabled (required for BLE on Android)
  Future<bool> _checkLocationEnabled() async {
    final status = await Permission.locationWhenInUse.serviceStatus;
    return status.isEnabled;
  }

  // Helper to compare UUIDs (handles different formats)
  bool _uuidMatches(dynamic uuid, String targetUuid) {
    final uuidStr = uuid.toString().toLowerCase().replaceAll('-', '');
    final targetStr = targetUuid.toLowerCase().replaceAll('-', '');
    return uuidStr == targetStr;
  }

  // BLE
  Future<void> _startBleScan() async {
    if (!await FlutterBluePlus.isSupported) {
      _showSnack('Bluetooth not supported');
      return;
    }
    
    // Check and request permissions
    if (Platform.isAndroid) {
      final permissions = [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.locationWhenInUse,
      ];
      
      for (var perm in permissions) {
        final status = await perm.request();
        if (status.isDenied || status.isPermanentlyDenied) {
          _showSnack('Permission ${perm.toString()} is required for BLE scanning');
          debugPrint('❌ Permission denied: ${perm.toString()}');
          return;
        }
      }
      
      // Check if location services are enabled
      if (!await _checkLocationEnabled()) {
        _showSnack('Please enable Location services in Settings');
        debugPrint('❌ Location services disabled');
        return;
      }
    }
    
    // Check Bluetooth adapter state
    final adapterState = await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      _showSnack('Please turn on Bluetooth');
      debugPrint('❌ Bluetooth adapter state: $adapterState');
      // Try to turn on Bluetooth
      if (Platform.isAndroid) {
        try {
          await FlutterBluePlus.turnOn();
          await Future.delayed(const Duration(seconds: 1));
          final newState = await FlutterBluePlus.adapterState.first;
          if (newState != BluetoothAdapterState.on) {
            debugPrint('❌ Failed to turn on Bluetooth');
            return;
          }
          debugPrint('✅ Bluetooth turned on');
        } catch (e) {
          debugPrint('❌ Error turning on Bluetooth: $e');
          return;
        }
      } else {
        return;
      }
    }
    
    if (!mounted) return;
    
    debugPrint('🔍 Starting BLE scan for: $_esp32DeviceName');
    showDialog(
      context: context,
      builder: (ctx) => _BleScanDialog(
        targetDeviceName: _esp32DeviceName,
        onSelect: (dev) {
          Navigator.pop(ctx);
          _connectBle(dev);
        },
      ),
    );
  }

  Future<void> _connectBle(BluetoothDevice dev) async {
    await _disconnect();
    setState(() => _status = 'Connecting...');
    
    StreamSubscription<BluetoothConnectionState>? stateSub;
    
    try {
      // Listen to connection state to detect disconnections
      stateSub = dev.connectionState.listen((state) {
        debugPrint('📶 Connection state: $state');
        if (state == BluetoothConnectionState.disconnected && _isConnected) {
          debugPrint('❌ Device disconnected unexpectedly');
          _disconnect();
        }
      });
      
      // Connect with longer timeout
      debugPrint('🔌 Connecting to ${dev.platformName}...');
      await dev.connect(
        license: License.free,
        timeout: const Duration(seconds: 30),
        autoConnect: false,
      );
      _bleDevice = dev;
      debugPrint('✅ Connected!');
      
      // Small delay before requesting MTU
      await Future.delayed(const Duration(milliseconds: 500));
      if (Platform.isAndroid) {
        try {
          final mtu = await dev.requestMtu(512);
          debugPrint('📏 MTU set to: $mtu');
        } catch (e) {
          debugPrint('⚠️ MTU request failed: $e (non-critical)');
        }
      }
      
      // Discover services with longer delay
      debugPrint('🔍 Discovering services...');
      await Future.delayed(const Duration(milliseconds: 800));
      final services = await dev.discoverServices();
      debugPrint('📡 Discovered ${services.length} services');
      
      // Look for our specific service UUID
      BluetoothService? targetService;
      for (var svc in services) {
        debugPrint('  Service: ${svc.uuid}');
        if (_uuidMatches(svc.uuid, _serviceUuid)) {
          targetService = svc;
          debugPrint('✅ Found target service!');
          break;
        }
      }
      
      if (targetService == null) {
        final allServices = services.map((s) => s.uuid.toString()).join('\n');
        setState(() => _status = 'Service not found\nFound: ${services.length} services\nTap to see details');
        debugPrint('❌ Target service $_serviceUuid not found');
        debugPrint('Available services:');
        for (var svc in services) {
          debugPrint('  - ${svc.uuid}');
          for (var chr in svc.characteristics) {
            debugPrint('    - Char: ${chr.uuid}');
          }
        }
        
        // Show dialog with all found services
        if (mounted) {
          showDialog(
            context: context,
            builder: (ctx) => AlertDialog(
              title: const Text('Service Not Found'),
              content: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Looking for:\n$_serviceUuid\n', style: const TextStyle(fontWeight: FontWeight.bold, color: Colors.red)),
                    Text('Found ${services.length} services:\n', style: const TextStyle(fontWeight: FontWeight.bold)),
                    ...services.map((svc) => Padding(
                      padding: const EdgeInsets.only(bottom: 8),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text('Service: ${svc.uuid}', style: const TextStyle(fontSize: 12, fontFamily: 'monospace')),
                          ...svc.characteristics.map((chr) => Text('  Char: ${chr.uuid}', style: const TextStyle(fontSize: 10, color: Colors.grey))),
                        ],
                      ),
                    )),
                  ],
                ),
              ),
              actions: [
                TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Close')),
              ],
            ),
          );
        }
        
        stateSub?.cancel();
        await dev.disconnect();
        return;
      }
      
      // Look for our specific characteristic UUID
      BluetoothCharacteristic? targetChar;
      for (var chr in targetService.characteristics) {
        debugPrint('  Characteristic: ${chr.uuid} - Notify: ${chr.properties.notify}');
        if (_uuidMatches(chr.uuid, _characteristicUuid)) {
          targetChar = chr;
          debugPrint('✅ Found target characteristic!');
          break;
        }
      }
      
      if (targetChar == null) {
        setState(() => _status = 'Characteristic not found');
        debugPrint('❌ Target characteristic $_characteristicUuid not found');
        debugPrint('Available characteristics:');
        for (var chr in targetService.characteristics) {
          debugPrint('  - ${chr.uuid}');
        }
        stateSub?.cancel();
        await dev.disconnect();
        return;
      }
      
      if (!targetChar.properties.notify) {
        setState(() => _status = 'No notify support');
        debugPrint('❌ Characteristic does not support notify');
        stateSub?.cancel();
        await dev.disconnect();
        return;
      }
      
      // Subscribe to notifications
      debugPrint('📲 Subscribing to notifications...');
      await targetChar.setNotifyValue(true);
      debugPrint('✅ Notifications enabled');
      
      _bleSub = targetChar.lastValueStream.listen(
        (v) {
          final data = utf8.decode(v);
          debugPrint('📥 BLE data: $data');
          _onData(data);
        },
        onError: (error) {
          debugPrint('❌ BLE data stream error: $error');
          _disconnect();
        },
      );
      
      setState(() { 
        _isConnected = true; 
        _connType = 'BLE'; 
        _status = 'BLE: ${dev.platformName}'; 
      });
      debugPrint('✅ BLE connected and subscribed');
      return;
    } catch (e) {
      setState(() => _status = 'BLE Error: $e');
      debugPrint('❌ Connection error: $e');
      // Clean up on error
      stateSub?.cancel();
      try {
        await dev.disconnect();
      } catch (_) {}
    }
  }

  Future<void> _disconnect() async {
    _simTimer?.cancel(); _simTimer = null;
    _udpSocket?.close(); _udpSocket = null;
    _bleSub?.cancel(); _bleSub = null;
    if (_bleDevice != null) {
      try { await _bleDevice?.disconnect(); } catch (_) {}
      _bleDevice = null;
    }
    if (mounted) setState(() { _isConnected = false; _connType = 'NONE'; _status = 'Disconnected'; });
  }

  // Fall Detection Alert
  void _triggerFallAlert() {
    setState(() => _fallDetected = true);
    
    // Start flashing effect
    _flashTimer?.cancel();
    _flashTimer = Timer.periodic(const Duration(milliseconds: 500), (timer) {
      if (mounted) setState(() => _flashState = !_flashState);
    });
    
    // Show dialog
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        backgroundColor: Colors.red[900],
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
        title: Column(
          children: [
            const Icon(Icons.warning_amber_rounded, color: Color.fromARGB(255, 118, 241, 2), size: 80),
            const SizedBox(height: 16),
            const Text(
              'FALL DETECTED!',
              style: TextStyle(
                color: Color.fromARGB(255, 39, 181, 0),
                fontSize: 28,
                fontWeight: FontWeight.bold,
              ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
        content: const Text(
          'The AI detected a potential fall event from user.',
          style: TextStyle(color: Colors.white70, fontSize: 16),
          textAlign: TextAlign.center,
        ),
        actions: [
          SizedBox(
            width: double.infinity,
            child: ElevatedButton(
              onPressed: () {
                _dismissFallAlert();
                Navigator.pop(ctx);
              },
              style: ElevatedButton.styleFrom(
                backgroundColor: Colors.white,
                foregroundColor: Colors.red[900],
                padding: const EdgeInsets.symmetric(vertical: 16),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
              ),
              child: const Text(
                "UNDERSTOOD",
                style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
              ),
            ),
          ),
        ],
      ),
    );
  }

  void _dismissFallAlert() {
    _flashTimer?.cancel();
    setState(() {
      _fallDetected = false;
      _flashState = false;
    });
  }

  // Simulation
  void _startSimulation() async {
    await _disconnect();
    setState(() { _isConnected = true; _connType = 'SIM'; _status = 'Simulating Data...'; });
    _simTimer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      final now = DateTime.now().millisecondsSinceEpoch;
      final t = now / 1000.0;
      final d = {
        'ts': now,
        'bat': 85 + (5 * sin(t * 0.1)).toInt(),
        'pitch': 30 * sin(t * 2),
        'roll': 30 * cos(t * 2),
        'svm': 1.0 + 0.5 * sin(t * 5),
        'gx': 100 * sin(t * 3),
        'gy': 100 * cos(t * 3),
        'gz': 50 * sin(t * 1),
        'hr': 75 + (15 * sin(t * 0.5)).toInt(), // 60-90 bpm
        'spo2': 96 + (3 * sin(t * 0.2)).toInt(), // 93-99 %
      };
      _onData(jsonEncode(d));
    });
  }

  // Recording
  Future<void> _toggleRecord() async {
    if (_isRecording) {
      setState(() => _isRecording = false);
      await _saveCsv();
    } else {
      _buffer.clear();
      setState(() => _isRecording = true);
    }
  }

  Future<void> _saveCsv() async {
    if (_buffer.isEmpty) return;
    try {
      if (kIsWeb) {
        _showSnack('File saving unavailable in Web Demo');
        return;
      }
      final rows = [['timestamp', 'pitch', 'roll', 'svm', 'gx', 'gy', 'gz', 'hr', 'spo2', 'label'], ..._buffer.map((d) => d.toCsvRow())];
      final csv = const ListToCsvConverter().convert(rows);
      final dir = await getExternalStorageDirectory();
      final path = '${dir!.path}/data_${DateTime.now().millisecondsSinceEpoch}.csv';
      await File(path).writeAsString(csv);
      _showSnack('Saved: $path');
    } catch (e) {
      _showSnack('Save error: $e');
    }
  }

  void _showSnack(String msg) {
    if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Widget _statChip(IconData icon, String label, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Row(children: [
        Icon(icon, size: 14, color: color),
        const SizedBox(width: 4),
        Text(label, style: TextStyle(color: color, fontWeight: FontWeight.bold, fontSize: 12)),
      ]),
    );
  }

  Widget _buildDualChart() {
    if (_pitchSpots.isEmpty) return const Center(child: Text('Waiting for data...', style: TextStyle(color: Colors.white54)));
    
    final isPortrait = MediaQuery.of(context).orientation == Orientation.portrait;
    
    // Chart 1: Pitch & Roll only (angles in degrees)
    final anglesChart = _buildSingleChart(
      spots: [_pitchSpots, _rollSpots],
      minY: -180.0,
      maxY: 180.0,
      title: 'ANGLES (°)',
      legend: [('Pitch', Colors.redAccent), ('Roll', Colors.greenAccent)],
    );
    
    // Chart 2: SVM (acceleration magnitude in g) - separate scale!
    final svmChart = _buildSingleChart(
      spots: [_svmSpots],
      minY: 0.0,
      maxY: 3.0,  // Normal range: 0.5-2.5g when moving
      title: 'ACCELERATION (g)',
      legend: [('SVM', Colors.orangeAccent)],
    );
    
    // Chart 3: Gyroscope (deg/s)
    final gyroChart = _buildSingleChart(
      spots: [_gxSpots, _gySpots, _gzSpots],
      minY: -500.0,
      maxY: 500.0,
      title: 'GYROSCOPE (°/s)',
      legend: [('X', Colors.redAccent), ('Y', Colors.greenAccent), ('Z', Colors.blueAccent)],
    );
    
    if (isPortrait) {
      return Column(
        children: [
          Expanded(flex: 2, child: anglesChart),
          const Divider(height: 1, color: Colors.white24),
          Expanded(flex: 1, child: svmChart),
          const Divider(height: 1, color: Colors.white24),
          Expanded(flex: 2, child: gyroChart),
        ],
      );
    } else {
      return Row(
        children: [
          Expanded(child: Column(
            children: [
              Expanded(child: anglesChart),
              const Divider(height: 1, color: Colors.white24),
              Expanded(child: svmChart),
            ],
          )),
          const VerticalDivider(width: 1, color: Colors.white24),
          Expanded(child: gyroChart),
        ],
      );
    }
  }
  
  Widget _buildSingleChart({
    required List<List<FlSpot>> spots,
    required double minY,
    required double maxY,
    required String title,
    required List<(String, Color)> legend,
  }) {
    return Padding(
      padding: const EdgeInsets.all(8),
      child: Column(
        children: [
          Text(title, style: const TextStyle(color: Colors.teal, fontWeight: FontWeight.bold, fontSize: 11)),
          const SizedBox(height: 4),
          Expanded(
            child: LineChart(
              LineChartData(
                gridData: FlGridData(show: true, drawVerticalLine: false, getDrawingHorizontalLine: (value) => const FlLine(color: Colors.white10, strokeWidth: 0.5)),
                titlesData: FlTitlesData(
                  show: true,
                  rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  bottomTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 40,
                      getTitlesWidget: (value, meta) => Text(
                        value.toInt().toString(),
                        style: const TextStyle(color: Colors.white54, fontSize: 10),
                      ),
                    ),
                  ),
                ),
                borderData: FlBorderData(show: true, border: Border.all(color: Colors.white24)),
                minY: minY,
                maxY: maxY,
                minX: spots[0].first.x,
                maxX: spots[0].last.x,
                lineBarsData: spots.asMap().entries.map((e) => 
                  _line(e.value, legend[e.key].$2)
                ).toList(),
              ),
              duration: Duration.zero,
            ),
          ),
          const SizedBox(height: 4),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: legend.map((e) => Padding(
              padding: const EdgeInsets.symmetric(horizontal: 6),
              child: Row(children: [
                Container(width: 12, height: 2, color: e.$2),
                const SizedBox(width: 4),
                Text(e.$1, style: const TextStyle(color: Colors.white70, fontSize: 10)),
              ]),
            )).toList(),
          ),
        ],
      ),
    );
  }

  LineChartBarData _line(List<FlSpot> spots, Color c) => LineChartBarData(
    spots: spots, color: c, isCurved: false, barWidth: 1.5, dotData: const FlDotData(show: false),
  );

  Widget _buildRawData() {
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _dataSection('ANGLES', [
            _dataRow('Pitch', _data['pitch'], Colors.redAccent, '°'),
            _dataRow('Roll', _data['roll'], Colors.greenAccent, '°'),
          ]),
          const SizedBox(height: 12),
          _dataSection('ACCELERATION', [
            _dataRow('SVM', _data['svm'], Colors.orangeAccent, 'g'),
          ]),
          const SizedBox(height: 12),
          _dataSection('GYROSCOPE', [
            _dataRow('X', _data['gx'], Colors.redAccent, '°/s'),
            _dataRow('Y', _data['gy'], Colors.greenAccent, '°/s'),
            _dataRow('Z', _data['gz'], Colors.blueAccent, '°/s'),
          ]),
          const SizedBox(height: 12),
          _dataSection('HEALTHq', [
            _dataRow('Heart Rate', _data['hr'], Colors.red, 'bpm'),
            _dataRow('SpO2', _data['spo2'], Colors.blue, '%'),
          ]),
        ],
      ),
    );
  }

  Widget _dataSection(String title, List<Widget> children) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(title, style: const TextStyle(color: Colors.teal, fontWeight: FontWeight.bold, fontSize: 12)),
      const SizedBox(height: 4),
      ...children,
    ],
  );

  Widget _dataRow(String label, dynamic val, Color c, String unit) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: const TextStyle(color: Colors.white70)),
        Text('${val is num ? val.toStringAsFixed(2) : val} $unit', 
          style: TextStyle(color: c, fontWeight: FontWeight.bold, fontFamily: 'monospace')),
      ],
    ),
  );

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        Scaffold(
          backgroundColor: const Color(0xFF241E4E),
          body: Column(
            children: [
              // Chart area
              Container(
                height: MediaQuery.of(context).size.height * 0.6,
                color: const Color(0xFF181330),
                padding: const EdgeInsets.fromLTRB(8, 40, 8, 8),
                child: Column(
              children: [
                // Controls
                Row(
                  children: [
                    // Battery indicator
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                      decoration: BoxDecoration(
                        color: Colors.teal.withValues(alpha: 0.3),
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Row(children: [
                        const Icon(Icons.battery_std, size: 14, color: Colors.greenAccent),
                        const SizedBox(width: 4),
                        Text('${_data['bat']}%', style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 12)),
                      ]),
                    ),
                    const SizedBox(width: 8),
                    // Heart Rate & SpO2
                    if (_data.containsKey('hr')) ...[
                      _statChip(Icons.favorite, '${_data['hr']}', Colors.red),
                      const SizedBox(width: 8),
                      _statChip(Icons.water_drop, '${_data['spo2']}%', Colors.blue),
                    ],
                    const Spacer(),
                    IconButton(
                      icon: Icon(_showGraph ? Icons.list : Icons.show_chart, color: Colors.white70),
                      onPressed: () => setState(() => _showGraph = !_showGraph),
                      tooltip: _showGraph ? 'Raw Data' : 'Graph',
                    ),
                  ],
                ),
                const SizedBox(height: 4),
                Expanded(
                  child: _showGraph ? _buildDualChart() : _buildRawData(),
                ),
              ],
            ),
          ),
          
          // Controls area
          Expanded(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  // Status - with text wrapping
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(
                        child: Text(
                          _status, 
                          style: TextStyle(
                            color: _isConnected ? const Color(0xFFFFD700) : Colors.grey, 
                            fontWeight: FontWeight.w500,
                            fontSize: 13,
                          ),
                          maxLines: 3,
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                        decoration: BoxDecoration(color: const Color(0xFFFFD700).withValues(alpha: 0.2), borderRadius: BorderRadius.circular(4)),
                        child: Text(_connType, style: const TextStyle(color: Color(0xFFFFD700), fontWeight: FontWeight.bold, fontSize: 12)),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  
                  // Label dropdown
                  DropdownButtonFormField<String>(
                    initialValue: _label,
                    decoration: InputDecoration(
                      labelText: 'Activity Label',
                      labelStyle: const TextStyle(color: Color(0xFFFFD700)),
                      border: const OutlineInputBorder(),
                      enabledBorder: OutlineInputBorder(borderSide: BorderSide(color: const Color(0xFFFFD700).withValues(alpha: 0.5))),
                      focusedBorder: const OutlineInputBorder(borderSide: BorderSide(color: Color(0xFFFFD700))),
                      isDense: true,
                    ),
                    style: const TextStyle(color: Color(0xFFFFD700)),
                    dropdownColor: const Color(0xFF1A1540),
                    items: _labels.map((l) => DropdownMenuItem(value: l, child: Text(l))).toList(),
                    onChanged: _isRecording ? null : (v) => setState(() => _label = v!),
                  ),
                  
                  const Spacer(),
                  
                  // Connect button
                  Row(
                    children: [
                      Expanded(
                        child: ElevatedButton.icon(
                          onPressed: _isConnected ? null : _showConnectDialog,
                          icon: const Icon(Icons.link),
                          label: const Text('CONNECT'),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: const Color(0xFF2196F3), foregroundColor: const Color(0xFFFFD700),
                            padding: const EdgeInsets.symmetric(vertical: 14),
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      IconButton.filled(
                        onPressed: _isConnected ? _disconnect : null,
                        icon: const Icon(Icons.power_settings_new),
                        style: IconButton.styleFrom(backgroundColor: Colors.red),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  
                  // Record button
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton.icon(
                      onPressed: _isConnected ? _toggleRecord : null,
                      icon: Icon(_isRecording ? Icons.stop : Icons.fiber_manual_record),
                      label: Text(_isRecording ? 'STOP & SAVE' : 'RECORD'),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: _isRecording ? Colors.red : Colors.indigo,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 14),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
        ),
        // Red flash overlay for fall detection
        if (_fallDetected && _flashState)
          Container(
            color: Colors.red.withValues(alpha: 0.4),
            child: Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(
                    Icons.warning_amber_rounded,
                    size: 120,
                    color: Colors.white.withValues(alpha: 0.9),
                  ),
                  const SizedBox(height: 20),
                  const Text(
                    'FALL DETECTED',
                    style: TextStyle(
                      color: Colors.white,
                      fontSize: 42,
                      fontWeight: FontWeight.bold,
                      shadows: [
                        Shadow(
                          blurRadius: 10,
                          color: Colors.black,
                          offset: Offset(2, 2),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
      ],
    );
  }

  void _showConnectDialog() {
    showDialog(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('Connect'),
        children: [
          ListTile(
            leading: const Icon(Icons.computer, color: Colors.purple),
            title: const Text('Simulation (Web/Test)'),
            subtitle: const Text('Random data'),
            onTap: () { Navigator.pop(ctx); _startSimulation(); },
          ),
          ListTile(
            leading: const Icon(Icons.wifi, color: Colors.blue),
            title: const Text('WiFi (UDP)'),
            subtitle: const Text('High speed'),
            onTap: () { Navigator.pop(ctx); _startUdp(); },
          ),
          ListTile(
            leading: const Icon(Icons.bluetooth, color: Colors.orange),
            title: const Text('Bluetooth LE'),
            subtitle: const Text('Low energy'),
            onTap: () { Navigator.pop(ctx); _startBleScan(); },
          ),
        ],
      ),
    );
  }
}

class _BleScanDialog extends StatefulWidget {
  final void Function(BluetoothDevice) onSelect;
  final String targetDeviceName;
  const _BleScanDialog({required this.onSelect, required this.targetDeviceName});

  @override
  State<_BleScanDialog> createState() => _BleScanDialogState();
}

class _BleScanDialogState extends State<_BleScanDialog> {
  List<ScanResult> _results = [];
  bool _scanning = true;
  StreamSubscription<List<ScanResult>>? _scanSub;

  @override
  void initState() {
    super.initState();
    _scan();
  }

  Future<void> _scan() async {
    // Clear previous results
    _results.clear();
    
    debugPrint('🔍 BLE Scan started...');
    
    // Listen to scan results
    _scanSub?.cancel();
    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      if (mounted) {
        setState(() {
          // Filter to show only devices with names (likely our ESP32)
          _results = results.where((r) => 
            r.device.platformName.isNotEmpty || 
            r.advertisementData.advName.isNotEmpty
          ).toList();
          
          // Log each device found
          for (var r in _results) {
            final name = r.device.platformName.isNotEmpty ? r.device.platformName : r.advertisementData.advName;
            debugPrint('  Device: $name | RSSI: ${r.rssi} | ID: ${r.device.remoteId}');
            if (name == widget.targetDeviceName) {
              debugPrint('  ✅ FOUND TARGET DEVICE!');
            }
          }
        });
      }
    });
    
    // Also listen to on-scan-results for immediate updates
    FlutterBluePlus.onScanResults.listen((results) {
      if (mounted && results.isNotEmpty) {
        setState(() {
          for (var r in results) {
            final idx = _results.indexWhere((e) => e.device.remoteId == r.device.remoteId);
            if (idx >= 0) {
              _results[idx] = r;
            } else if (r.device.platformName.isNotEmpty || r.advertisementData.advName.isNotEmpty) {
              _results.add(r);
            }
          }
        });
      }
    });
    
    try {
      // Start scan with Android-specific settings for better discovery
      // NO service UUID filters - NimBLE doesn't include UUIDs in advertising packet
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 15),
        androidScanMode: AndroidScanMode.lowLatency,
      );
      debugPrint('📡 BLE Scan running (15s timeout, low latency mode)');
    } catch (e) {
      debugPrint('❌ Scan error: $e');
    }
    
    if (mounted) setState(() => _scanning = false);
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // Sort results: target device first
    _results.sort((a, b) {
      final aName = a.device.platformName.isNotEmpty ? a.device.platformName : a.advertisementData.advName;
      final bName = b.device.platformName.isNotEmpty ? b.device.platformName : b.advertisementData.advName;
      if (aName == widget.targetDeviceName) return -1;
      if (bName == widget.targetDeviceName) return 1;
      return b.rssi.compareTo(a.rssi); // Sort by signal strength
    });
    
    return AlertDialog(
      title: Row(children: [
        if (_scanning) const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)),
        if (_scanning) const SizedBox(width: 8),
        Text(_scanning ? 'Scanning...' : 'Devices'),
        const Spacer(),
        Text('Looking for: ${widget.targetDeviceName}', style: const TextStyle(fontSize: 10, color: Colors.grey)),
      ]),
      content: SizedBox(
        width: 300, height: 250,
        child: _results.isEmpty
          ? Center(child: _scanning 
            ? Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const CircularProgressIndicator(),
                  const SizedBox(height: 16),
                  Text('Searching for ${widget.targetDeviceName}...', style: const TextStyle(fontSize: 12)),
                ],
              )
            : const Text('No devices found'))
          : ListView.builder(
              itemCount: _results.length,
              itemBuilder: (_, i) {
                final r = _results[i];
                // Try platformName first, then advertisementData.advName
                String name = r.device.platformName;
                if (name.isEmpty) name = r.advertisementData.advName;
                if (name.isEmpty) name = r.device.remoteId.toString();
                
                final isTargetDevice = name == widget.targetDeviceName;
                
                return Container(
                  decoration: isTargetDevice ? BoxDecoration(
                    color: Colors.green.withValues(alpha: 0.2),
                    border: Border.all(color: Colors.green, width: 2),
                    borderRadius: BorderRadius.circular(8),
                  ) : null,
                  margin: const EdgeInsets.symmetric(vertical: 2),
                  child: ListTile(
                    leading: Icon(
                      isTargetDevice ? Icons.bluetooth_connected : Icons.bluetooth,
                      color: isTargetDevice ? Colors.green : (r.rssi > -70 ? Colors.blue : Colors.grey),
                    ),
                    title: Row(
                      children: [
                        Expanded(child: Text(name, style: TextStyle(fontWeight: isTargetDevice ? FontWeight.bold : FontWeight.normal))),
                        if (isTargetDevice) const Icon(Icons.check_circle, color: Colors.green, size: 16),
                      ],
                    ),
                    subtitle: Text('${r.rssi} dBm • ${r.device.remoteId}'),
                    trailing: TextButton(
                      child: const Text('Connect'), 
                      onPressed: () => widget.onSelect(r.device),
                    ),
                  ),
                );
              },
            ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
        if (!_scanning) TextButton(onPressed: () { setState(() { _results.clear(); _scanning = true; }); _scan(); }, child: const Text('Rescan')),
      ],
    );
  }
}
