# Project Brief: GPS-Denied Autonomous Drone Navigation

> Dokumen ini adalah konteks utama proyek. Baca dan pahami seluruhnya sebelum mengerjakan task apa pun. Bahasa kerja: Bahasa Indonesia (istilah teknis tetap dalam bahasa Inggris).

## 1. Visi Proyek

Mengembangkan sistem drone yang mampu **menjalankan misi penerbangan otonom tanpa GPS sama sekali**, dengan navigasi berbasis *dead reckoning* modern: fusi data optical flow, rangefinder (lidar), IMU (accelerometer + gyroscope), dan kompas. Misi tidak lagi didefinisikan sebagai koordinat global (latitude/longitude), melainkan sebagai **titik-titik relatif dalam meter terhadap titik home** — mirip cara navigasi pelaut sebelum era GPS: arah (kompas) dan jarak tempuh.

Target proyek:
1. **Jangka pendek**: sistem terbukti bekerja di simulasi (SITL) dan hardware nyata, dengan data drift terukur → publikasi jurnal ilmiah.
2. **Jangka panjang**: produk standar untuk penerbangan drone GPS-independent (indoor, area urban canyon, area GPS-jammed).

## 2. Latar Belakang Ilmiah

- **Masalah**: navigasi drone konvensional bergantung penuh pada GNSS. Di lingkungan indoor, bawah kanopi, urban canyon, atau kondisi jamming/spoofing, GNSS tidak tersedia atau tidak dapat dipercaya.
- **Pendekatan**: ArduPilot EKF3 (Extended Kalman Filter) mendukung *source switching* — posisi/velocity horizontal dapat diambil dari **optical flow odometry** (sensor mengukur pergeseran visual permukaan tanah, dikombinasikan dengan tinggi dari rangefinder untuk konversi rad/s → m/s), heading dari **magnetometer (compass)**, dan altitude dari **barometer/rangefinder**. Integrasi velocity terhadap waktu menghasilkan estimasi posisi dalam **local NED frame** (North-East-Down, origin = titik arming/home).
- **Konsekuensi ilmiah yang harus diukur**: tanpa koreksi absolut (GPS), error posisi **terakumulasi seiring waktu dan jarak (drift)**. Metrik utama riset ini adalah karakterisasi drift: return-to-home error setelah misi berpola diketahui (mis. box mission 20×20 m), sebagai fungsi jarak tempuh, kecepatan, dan tekstur permukaan.
- **Ground truth**: GPS tetap dipasang HANYA sebagai pembanding (ground truth) untuk analisis drift — tidak pernah dipakai oleh EKF sebagai sumber navigasi.

## 3. Ruang Lingkup Pengembangan

### 3.1 Modifikasi QGroundControl (fokus utama pengembangan software)

QGC standar menampilkan **peta dunia (map view)** sebagai tampilan utama — tidak relevan untuk penerbangan tanpa koordinat global. Modifikasi yang diinginkan:

1. **Ganti map view dengan "Local Grid View"**: bidang kartesius 2D yang merepresentasikan local NED frame.
   - Origin (0,0) = posisi home/arming, ditandai jelas.
   - **Grid kotak-kotak berskala meter** (mis. 1 kotak = 5 m, dapat di-zoom).
   - **Compass rose / anotasi derajat**: Utara = 0°, Timur = 90°, Selatan = 180°, Barat = 270° (konvensi heading aviasi, searah jarum jam).
   - Posisi drone real-time digambar dari data telemetri `LOCAL_POSITION_NED` (MAVLink), lengkap dengan arah heading (dari `ATTITUDE.yaw`) dan jejak lintasan (trail).
   - Waypoint misi ditampilkan dan dapat dibuat langsung di grid ini (klik → titik relatif dalam meter, bukan lat/lon).
2. **Panel status sensor navigasi non-GPS**: menampilkan kesehatan dan nilai real-time sensor yang kritikal untuk mode ini — optical flow quality, rangefinder distance, compass heading & health, EKF innovation/variance, estimated velocity (vx, vy, vz), estimated position (x, y, z lokal).
3. Titik masuk kode yang sudah diidentifikasi: `src/MissionManager` (logika misi), plus modul FlightDisplay/FlyView (QML) untuk tampilan. Eksplorasi arsitektur QGC lebih lanjut adalah bagian dari pekerjaan.

### 3.2 Konfigurasi ArduPilot (bukan modifikasi firmware dulu)

Fase awal cukup **konfigurasi parameter** (tanpa mengubah source ArduPilot):
`SIM_FLOW_ENABLE=1, FLOW_TYPE=10, RNGFND1_TYPE=100, RNGFND1_MAX=1200, EK3_SRC1_POSXY=0, EK3_SRC1_VELXY=5, EK3_SRC1_POSZ=2, EK3_SRC1_YAW=1, SIM_GPS1_ENABLE=0, GPS1_TYPE=0`
Modifikasi source ArduPilot hanya jika riset menuntutnya (mis. perilaku mission engine terhadap waypoint lokal).

### 3.3 Mission Script (Python, terpisah)

Project terpisah di `/Users/mc/PycharmProjects/gps-denied-mission`:
- `mission_runner.py` — pymavlink, koneksi `udp:127.0.0.1:14551` (port 14550 dipakai QGC), mode GUIDED, perintah `SET_POSITION_TARGET_LOCAL_NED`.
- Misi pertama: box 20×20 m, ukur return-to-home error sebagai data drift pertama.

## 4. Arsitektur Sistem

```
[Sensor fisik / SITL simulated]
 MicoAir MTF-01P (optical flow + lidar 12 m) ── UART/MSP ──┐
 IMU (accel + gyro, onboard FC) ───────────────────────────┤
 Compass (magnetometer) ───────────────────────────────────┤
 Barometer ────────────────────────────────────────────────┤
                                                           ▼
                                    [Flight Controller: ArduPilot Copter]
                                     EKF3 fusion → estimasi posisi local NED
                                                           │ MAVLink
                              ┌────────────────────────────┼───────────────────────┐
                              ▼                            ▼                       ▼
                    [QGC modifikasi]              [mission_runner.py]      [GPS (ground truth
                    Local Grid View +              pymavlink, GUIDED,       only, logging)]
                    panel sensor non-GPS           waypoint relatif
```

Pembagian tim: **Gabriel (informatika)** — algoritma misi, modifikasi QGC, analisis data. **Mitra (teknik elektro)** — sistem hardware, integrasi sensor, memastikan kualitas data sensor.

## 5. Alat & Bahan (Hardware)

| Komponen | Spesifikasi / Kandidat | Fungsi |
|---|---|---|
| Optical flow + lidar | MicoAir MTF-01P (flow + rangefinder s.d. 12 m) | Velocity horizontal + altitude AGL |
| Flight controller | ArduPilot-compatible (mis. Pixhawk/Matek H743) | EKF3, mission engine |
| Compass | Magnetometer eksternal (jauh dari motor/ESC) | Heading absolut (yaw source) |
| IMU | Onboard FC | Accelerasi & rotasi |
| GPS | Modul GNSS standar | **Ground truth saja**, bukan navigasi |
| Frame + motor + ESC + baterai | Quadcopter kelas riset | Platform uji |
| Companion/telemetry | Telemetry radio / WiFi | Link MAVLink ke ground station |

## 6. Lingkungan Pengembangan (SUDAH TERPASANG & TERVERIFIKASI)

- **Mesin**: MacBook Pro M1 Pro (arm64), macOS Tahoe.
- **IDE**: CLion (C++/QGC, toolchain System: AppleClang via ccache Homebrew, CMake bundled 4.2.2, Ninja, LLDB) dan PyCharm (Python mission scripts).
- **QGroundControl**: source di `/Users/mc/CLionProjects/qgroundcontrol`, **branch master** (requirement: Qt ≥ 6.11.0, CMake ≥ 3.25). Build SUKSES dengan Qt 6.11.1 + GStreamer 1.28.4 (runtime + devel, framework resmi). Bootstrap Python via `uv`.
- **ArduPilot**: source di `/Users/mc/CLionProjects/ardupilot`, SITL build sukses, MAVProxy console + map berjalan. Python 3.10.18 (pyenv) dalam `.venv`.
- **Alur uji**: SITL (`sim_vehicle.py --console --map`) → QGC auto-connect UDP 14550 → mission script di 14551 (via `output add` MAVProxy).
- **Catatan penting**: master QGC adalah moving target — **commit hash basis pengembangan harus dicatat** dan pekerjaan dilakukan di branch sendiri (mis. `gps-denied-mission`) demi reproducibility publikasi.
- **Basis pengembangan (dicatat 2026-07-15)**: branch `gps-denied-mission` dari master commit `22173690e` (v5.0.3-1116). Master lokal dijaga identik dengan upstream. Remote: `origin` = repo private `and0789/qgroundcontrol` (SSH), `upstream` = `mavlink/qgroundcontrol`.

## 7. Metodologi Pengujian

1. **SITL murni** (fase sekarang): validasi konfigurasi non-GPS, misi box, ukur drift simulasi.
2. **SITL + QGC modifikasi**: validasi Local Grid View menampilkan posisi/misi dengan benar terhadap data `LOCAL_POSITION_NED`.
3. **Hardware bench test**: validasi kualitas data sensor (flow quality, rangefinder noise) sebelum terbang.
4. **Flight test bertahap**: hover → misi pendek → box 20×20 m, GPS logging sebagai ground truth, analisis drift.
5. **Metrik utama**: return-to-home error (m), drift per meter tempuh (m/m), konsistensi antar-run (standar deviasi).

## 8. Tujuan Pembelajaran (PENTING untuk cara AI merespons)

Gabriel ingin **belajar menyusun program dan proyek yang baik**, bukan sekadar mendapat kode jadi. Maka setiap kontribusi AI harus:
- Menjelaskan **alasan (why)** di balik keputusan teknis dan arsitektural, bukan hanya caranya.
- Mengikuti praktik software engineering yang baik: struktur modul jelas, commit kecil dan bermakna, dokumentasi, testing.
- Menyebutkan implikasi terhadap **publikasi jurnal** bila relevan (reproducibility, metrik, referensi).
- Bahasa Indonesia, perintah praktis spesifik-macOS.

## 9. Yang Diminta dari AI Selanjutnya

Susun **roadmap pengembangan terstruktur** (proyek berjalan ±10–12 bulan, saat ini di fase simulasi SITL) yang mencakup: fase-fase dengan deliverable dan kriteria selesai yang terukur, urutan modifikasi QGC (mulai dari eksplorasi arsitektur QML/C++ QGC → prototipe Local Grid View → panel sensor → mission planning lokal), strategi testing per fase, pembagian tugas Gabriel vs mitra elektro, serta milestone data untuk paper.

## 10. Known Issues & Batasan

- QGC Stable 5.0.8 build lokal crash saat quit (SIGSEGV di `QGCApplication.cc` via `applicationShouldTerminate`, macOS Tahoe) — kosmetik; basis pengembangan kini master.
- Optical flow butuh permukaan bertekstur dan pencahayaan cukup; rangefinder maks 12 m → batasi altitude uji.
- Drift tak terhindarkan tanpa koreksi absolut — riset ini MENGUKUR dan MEMINIMALKAN, bukan menghilangkan.

## Glosarium Singkat

- **NED**: North-East-Down, kerangka koordinat lokal; origin di titik arming.
- **Dead reckoning**: estimasi posisi dari integrasi arah + kecepatan/jarak terhadap waktu.
- **EKF3**: Extended Kalman Filter ArduPilot untuk fusi multi-sensor.
- **Optical flow odometry**: estimasi velocity dari pergeseran visual permukaan + tinggi AGL.
- **SITL**: Software In The Loop, simulasi ArduPilot tanpa hardware.
- **Drift**: akumulasi error posisi seiring waktu/jarak pada navigasi relatif.
