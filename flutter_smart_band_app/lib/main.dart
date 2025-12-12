import 'dart:async';
import 'dart:convert';
import 'dart:io';

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
  // Connection
  RawDatagramSocket? _udpSocket;
  BluetoothDevice? _bleDevice;
  StreamSubscription<List<int>>? _bleSub;
  bool _isConnected = false;
  String _connType = 'NONE';
  String _status = 'Disconnected';

  // Data
  Map<String, dynamic> _data = {'ts': 0, 'bat': 0, 'pitch': 0.0, 'roll': 0.0, 'svm': 0.0, 'gx': 0.0, 'gy': 0.0, 'gz': 0.0};
  
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

  @override
  void dispose() {
    _disconnect();
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
      
      if (_isRecording) {
        _buffer.add(SensorData(
          timestamp: d['ts'], pitch: d['pitch'].toDouble(), roll: d['roll'].toDouble(),
          svm: d['svm'].toDouble(), gx: d['gx'].toDouble(), gy: d['gy'].toDouble(), 
          gz: d['gz'].toDouble(), label: _label,
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

  // BLE
  Future<void> _startBleScan() async {
    if (!await FlutterBluePlus.isSupported) {
      _showSnack('Bluetooth not supported');
      return;
    }
    if (await FlutterBluePlus.adapterState.first != BluetoothAdapterState.on) {
      _showSnack('Please turn on Bluetooth');
      return;
    }
    await [Permission.bluetoothScan, Permission.bluetoothConnect, Permission.location].request();
    if (!mounted) return;
    
    showDialog(
      context: context,
      builder: (ctx) => _BleScanDialog(onSelect: (dev) {
        Navigator.pop(ctx);
        _connectBle(dev);
      }),
    );
  }

  Future<void> _connectBle(BluetoothDevice dev) async {
    await _disconnect();
    setState(() => _status = 'Connecting...');
    try {
      await dev.connect(license: License.free);
      _bleDevice = dev;
      if (Platform.isAndroid) await dev.requestMtu(512);
      
      for (var svc in await dev.discoverServices()) {
        for (var chr in svc.characteristics) {
          if (chr.properties.notify) {
            await chr.setNotifyValue(true);
            _bleSub = chr.lastValueStream.listen((v) => _onData(utf8.decode(v)));
            setState(() { _isConnected = true; _connType = 'BLE'; _status = 'BLE: ${dev.platformName}'; });
            return;
          }
        }
      }
      setState(() => _status = 'No notify characteristic');
      await dev.disconnect();
    } catch (e) {
      setState(() => _status = 'BLE Error: $e');
    }
  }

  Future<void> _disconnect() async {
    _udpSocket?.close(); _udpSocket = null;
    _bleSub?.cancel(); _bleSub = null;
    await _bleDevice?.disconnect(); _bleDevice = null;
    if (mounted) setState(() { _isConnected = false; _connType = 'NONE'; _status = 'Disconnected'; });
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
      final rows = [['timestamp', 'pitch', 'roll', 'svm', 'gx', 'gy', 'gz', 'label'], ..._buffer.map((d) => d.toCsvRow())];
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

  Widget _buildDualChart() {
    if (_pitchSpots.isEmpty) return const Center(child: Text('Waiting for data...', style: TextStyle(color: Colors.white54)));
    
    final isPortrait = MediaQuery.of(context).orientation == Orientation.portrait;
    
    final anglesChart = _buildSingleChart(
      spots: [_pitchSpots, _rollSpots, _svmSpots],
      minY: -180.0,
      maxY: 180.0,
      title: 'ANGLES',
      legend: [('Pitch', Colors.redAccent), ('Roll', Colors.greenAccent), ('SVM', Colors.blueAccent)],
    );
    
    final gyroChart = _buildSingleChart(
      spots: [_gxSpots, _gySpots, _gzSpots],
      minY: -500.0,
      maxY: 500.0,
      title: 'GYROSCOPE',
      legend: [('X', Colors.redAccent), ('Y', Colors.greenAccent), ('Z', Colors.blueAccent)],
    );
    
    if (isPortrait) {
      return Column(
        children: [
          Expanded(child: anglesChart),
          const Divider(height: 1, color: Colors.white24),
          Expanded(child: gyroChart),
        ],
      );
    } else {
      return Row(
        children: [
          Expanded(child: anglesChart),
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
                lineBarsData: [
                  _line(spots[0], legend[0].$2),
                  _line(spots[1], legend[1].$2),
                  _line(spots[2], legend[2].$2),
                ],
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
            _dataRow('SVM', _data['svm'], Colors.blueAccent, 'g'),
          ]),
          const SizedBox(height: 12),
          _dataSection('GYROSCOPE', [
            _dataRow('X', _data['gx'], Colors.redAccent, '°/s'),
            _dataRow('Y', _data['gy'], Colors.greenAccent, '°/s'),
            _dataRow('Z', _data['gz'], Colors.blueAccent, '°/s'),
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
    return Scaffold(
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
                      child: Text('${_data['bat']}%', style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 12)),
                    ),
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
                  // Status
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text(_status, style: TextStyle(color: _isConnected ? const Color(0xFFFFD700) : Colors.grey, fontWeight: FontWeight.w500)),
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
    );
  }

  void _showConnectDialog() {
    showDialog(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('Connect'),
        children: [
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
  const _BleScanDialog({required this.onSelect});

  @override
  State<_BleScanDialog> createState() => _BleScanDialogState();
}

class _BleScanDialogState extends State<_BleScanDialog> {
  List<ScanResult> _results = [];
  bool _scanning = true;

  @override
  void initState() {
    super.initState();
    _scan();
  }

  Future<void> _scan() async {
    FlutterBluePlus.scanResults.listen((r) { if (mounted) setState(() => _results = r); });
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 8));
    if (mounted) setState(() => _scanning = false);
  }

  @override
  void dispose() {
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Row(children: [
        if (_scanning) const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)),
        if (_scanning) const SizedBox(width: 8),
        Text(_scanning ? 'Scanning...' : 'Devices'),
      ]),
      content: SizedBox(
        width: 300, height: 250,
        child: _results.isEmpty
          ? Center(child: _scanning ? const CircularProgressIndicator() : const Text('No devices found'))
          : ListView.builder(
              itemCount: _results.length,
              itemBuilder: (_, i) {
                final r = _results[i];
                final name = r.device.platformName.isEmpty ? 'Unknown' : r.device.platformName;
                return ListTile(
                  leading: Icon(Icons.bluetooth, color: r.rssi > -70 ? Colors.blue : Colors.grey),
                  title: Text(name),
                  subtitle: Text('${r.rssi} dBm'),
                  trailing: TextButton(child: const Text('Connect'), onPressed: () => widget.onSelect(r.device)),
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
