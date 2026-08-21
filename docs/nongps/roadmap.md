# Roadmap Pengembangan: Drone Navigasi Otonom Tanpa GPS

*Navigasi berbasis fusi Optical Flow, Lidar, IMU, dan Kompas (ArduPilot EKF3)*

Durasi: ±10–12 bulan • Status saat ini: Fase 2 (Simulasi SITL) • Juli 2026

## 1. Ringkasan Eksekutif

Proyek ini mengembangkan sistem drone yang mampu menjalankan misi penerbangan otonom tanpa GPS. Navigasi mengandalkan dead reckoning modern: fusi optical flow, rangefinder lidar, IMU, dan kompas melalui EKF3 ArduPilot, dengan misi didefinisikan sebagai titik relatif dalam meter terhadap titik home (local NED frame) — bukan koordinat lintang/bujur. Ground station QGroundControl dimodifikasi: tampilan peta dunia diganti Local Grid View (bidang kartesius berskala meter dengan anotasi derajat kompas, Utara = 0°) dan dilengkapi panel status sensor navigasi non-GPS.

Keluaran akhir: (1) sistem terbukti bekerja di simulasi dan hardware nyata dengan karakterisasi drift yang terukur, dan (2) naskah publikasi jurnal ilmiah. GPS tetap dipasang hanya sebagai ground truth untuk analisis drift, tidak pernah dipakai sebagai sumber navigasi.

Tim: Gabriel (informatika) — algoritma misi, modifikasi QGroundControl, analisis data; mitra teknik elektro — sistem hardware, integrasi sensor, dan jaminan kualitas data sensor.

## 2. Timeline Ringkas

| Fase | Fokus | Perkiraan waktu | Status |
|----|----|----|----|
| 1 | Persiapan lingkungan & studi literatur | Bulan 1–2 | **SELESAI** |
| 2 | Simulasi SITL non-GPS + skrip misi | Bulan 2–4 | **BERJALAN** |
| 3 | Integrasi hardware & bench test sensor | Bulan 4–6 | Belum mulai |
| 4 | Uji terbang dasar non-GPS & data drift awal | Bulan 6–8 | Belum mulai |
| 5 | Modifikasi QGroundControl (Local Grid View) | Bulan 7–10 (paralel) | Belum mulai |
| 6 | Uji terpadu, data final & penulisan paper | Bulan 10–12 | Belum mulai |

*Catatan: Fase 5 dapat berjalan paralel dengan Fase 4 karena modifikasi QGC diuji terhadap SITL, tidak menunggu hardware siap. Overlap ini menjaga total durasi tetap 10–12 bulan.*

## 3. Detail Fase

### Fase 1 — Persiapan Lingkungan & Studi Literatur

|  |  |
|----|----|
| **Waktu** | Bulan 1–2 |
| **Status** | **SELESAI ✔** |
| **Tujuan** | Lingkungan pengembangan siap dan tervalidasi; dasar teori navigasi non-GPS dikuasai. |
| **Kegiatan utama** | Build ArduPilot SITL di macOS (M1 Pro, Tahoe); build QGroundControl dari source (branch master, Qt 6.11.1, GStreamer 1.28.4, bootstrap uv); setup CLion (toolchain AppleClang + ccache) dan PyCharm; studi EKF3, optical flow odometry, local NED frame. |
| **Deliverable** | Dokumentasi setup — lingkungan tercatat di [project-brief.md](project-brief.md) §6, prosedur menjalankannya di [prosedur-terbang-non-gps-sitl.md](prosedur-terbang-non-gps-sitl.md); SITL berjalan dengan MAVProxy console + map; QGC build sukses dan terhubung ke SITL. |
| **Kriteria selesai** | Semua toolchain build tanpa error; koneksi QGC–SITL terverifikasi. (Tercapai 15 Juli 2026.) |
| **Alat yang dibutuhkan** | MacBook Pro M1 Pro (macOS Tahoe), CLion, PyCharm, Qt 6.11.1, CMake ≥3.25, Ninja, ccache, uv, Python 3.10 (pyenv), GStreamer 1.28.4, Git. |

### Fase 2 — Simulasi SITL Non-GPS & Skrip Misi

|  |  |
|----|----|
| **Waktu** | Bulan 2–4 |
| **Status** | **BERJALAN ►** |
| **Tujuan** | Membuktikan konsep navigasi tanpa GPS sepenuhnya di simulasi dan membangun kerangka pengukuran drift. |
| **Kegiatan utama** | Konfigurasi parameter non-GPS (EK3_SRC1_VELXY=5 optical flow, EK3_SRC1_YAW=1 compass, SIM_GPS1_ENABLE=0, dst.); pengembangan mission_runner.py (pymavlink, GUIDED, SET_POSITION_TARGET_LOCAL_NED, port UDP 14551); misi box 20×20 m; logging LOCAL_POSITION_NED vs ground truth simulator; otomasi run berulang untuk statistik. |
| **Deliverable** | Skrip misi berfungsi penuh; dataset drift simulasi (≥20 run box mission); notebook analisis (return-to-home error, drift per meter). |
| **Kriteria selesai** | Drone SITL menyelesaikan box mission tanpa GPS secara konsisten; pipeline logging & analisis menghasilkan metrik drift otomatis. |
| **Alat yang dibutuhkan** | SITL + MAVProxy, QGC (monitoring), PyCharm, pymavlink, pandas/matplotlib untuk analisis, repositori Git terpisah (gps-denied-mission). |

### Fase 3 — Integrasi Hardware & Bench Test Sensor

|  |  |
|----|----|
| **Waktu** | Bulan 4–6 |
| **Status** | **Belum mulai** |
| **Tujuan** | Platform fisik siap terbang dengan data sensor berkualitas — tanggung jawab utama mitra teknik elektro, dengan validasi data oleh Gabriel. |
| **Kegiatan utama** | Perakitan quadcopter; wiring MTF-01P ke flight controller (UART, protokol MSP); kalibrasi kompas jauh dari interferensi motor/ESC; kalibrasi IMU & accelerometer; bench test: verifikasi flow quality, noise rangefinder, stabilitas heading; konfigurasi parameter non-GPS di hardware nyata. |
| **Deliverable** | Drone rakitan lengkap; laporan bench test kualitas sensor; checklist pra-terbang; konfigurasi parameter terdokumentasi. |
| **Kriteria selesai** | Flow quality stabil di atas ambang pada permukaan uji; rangefinder akurat pada rentang kerja; heading kompas konsisten (deviasi dalam batas wajar saat motor berputar). |
| **Alat yang dibutuhkan** | Lihat Bagian 4 (daftar hardware lengkap): frame, FC, MTF-01P, kompas eksternal, GPS (ground truth), ESC, motor, propeler, baterai LiPo + charger, telemetry radio; perkakas: solder & timah, multimeter, obeng/hex set, tie-strap, bench power supply (opsional), area uji indoor bertekstur. |

### Fase 4 — Uji Terbang Dasar Non-GPS & Data Drift Awal

|  |  |
|----|----|
| **Waktu** | Bulan 6–8 |
| **Status** | **Belum mulai** |
| **Tujuan** | Membuktikan penerbangan non-GPS di dunia nyata dan mengumpulkan data drift awal untuk paper. |
| **Kegiatan utama** | Uji bertahap: hover rendah → gerakan pendek → box mission 20×20 m; GPS logging sebagai ground truth (bukan navigasi); variasi permukaan/tekstur dan kecepatan; analisis perbandingan drift nyata vs simulasi. |
| **Deliverable** | Log penerbangan lengkap (dataflash + telemetri); dataset drift dunia nyata; analisis perbandingan SITL vs realita (bahan inti paper). |
| **Kriteria selesai** | Drone menyelesaikan box mission non-GPS di lapangan dengan aman dan berulang; return-to-home error terukur pada ≥10 penerbangan valid. |
| **Alat yang dibutuhkan** | Drone hasil Fase 3, lokasi uji aman (lapangan bertekstur, angin rendah), baterai cadangan, remote control (failsafe manual), laptop lapangan + telemetry, alat keselamatan (fire-safe bag LiPo, P3K), izin/kepatuhan regulasi setempat. |

### Fase 5 — Modifikasi QGroundControl — Local Grid View

|  |  |
|----|----|
| **Waktu** | Bulan 7–10 (paralel dgn Fase 4) |
| **Status** | **Belum mulai** |
| **Tujuan** | Ground station yang sepenuhnya berorientasi navigasi lokal: perencanaan dan pemantauan misi dalam meter dan derajat, tanpa peta dunia. |
| **Kegiatan utama** | Eksplorasi arsitektur QGC (QML FlyView, C++ src/MissionManager, Vehicle/FactSystem); prototipe Local Grid View: grid kartesius berskala meter, origin di home, compass rose 0°=Utara, posisi drone real-time dari LOCAL_POSITION_NED + heading dari ATTITUDE, trail lintasan; panel sensor non-GPS (flow quality, rangefinder, compass health, EKF variance, velocity & posisi lokal); mission planning klik-di-grid (waypoint relatif meter); kerja di branch sendiri dari commit hash tercatat. |
| **Deliverable** | Branch gps-denied-mission pada fork QGC; Local Grid View fungsional; panel sensor; dokumentasi arsitektur modifikasi (bahan bagian implementasi paper). |
| **Kriteria selesai** | Misi dapat dibuat, dikirim, dan dipantau sepenuhnya dari Local Grid View terhadap SITL; posisi tampilan cocok dengan log LOCAL_POSITION_NED. |
| **Alat yang dibutuhkan** | CLion + toolchain QGC (sudah siap dari Fase 1), SITL sebagai target uji, Qt Design/QML tooling, Git (worktree/branch), Claude Code + CLAUDE.md sebagai asisten pengembangan. |

### Fase 6 — Uji Terpadu, Data Final & Penulisan Paper

|  |  |
|----|----|
| **Waktu** | Bulan 10–12 |
| **Status** | **Belum mulai** |
| **Tujuan** | Sistem lengkap teruji end-to-end dan naskah jurnal siap submit. |
| **Kegiatan utama** | Uji terpadu: misi direncanakan di QGC modifikasi → dieksekusi drone nyata non-GPS → dipantau di Local Grid View; pengumpulan dataset final; penulisan naskah (metodologi, hasil drift, pembahasan); persiapan reproducibility (commit hash, parameter, skrip analisis). |
| **Deliverable** | Dataset final + skrip analisis terbuka; naskah jurnal tersubmit; video demo sistem. |
| **Kriteria selesai** | Naskah selesai dan disubmit ke jurnal target; seluruh eksperimen dapat direproduksi dari dokumentasi. |
| **Alat yang dibutuhkan** | Seluruh sistem hasil fase sebelumnya, template jurnal target, reference manager (Zotero/Mendeley), penyimpanan data (backup). |

## 4. Daftar Alat & Bahan

### 4.1 Perangkat Lunak (sudah terpasang & tervalidasi)

| Perangkat | Versi / Konfigurasi | Fungsi |
|----|----|----|
| macOS + MacBook M1 Pro | macOS Tahoe, arm64 | Mesin pengembangan utama |
| CLion | Toolchain System (AppleClang via ccache), CMake ≥3.25, Ninja, LLDB | IDE C++ untuk QGC |
| PyCharm + Python | Python 3.10 (pyenv, .venv) | Skrip misi & analisis data |
| QGroundControl source | Branch master (commit hash dicatat), Qt 6.11.1, GStreamer 1.28.4 | Ground station yang dimodifikasi |
| ArduPilot source + SITL | ArduCopter, sim_vehicle.py + MAVProxy | Firmware & simulasi |
| pymavlink, pandas, matplotlib | Dalam .venv proyek misi | Kontrol misi & analisis drift |
| Git + GitHub | Fork + branch gps-denied-mission | Version control & reproducibility |
| Claude Code + CLAUDE.md | Brief proyek di root repo | Asisten pengembangan AI |

### 4.2 Perangkat Keras

| Komponen | Spesifikasi / Kandidat | Fungsi & Catatan |
|----|----|----|
| Sensor optical flow + lidar | MicoAir MTF-01P (flow + rangefinder s.d. 12 m, UART/MSP) | Sumber velocity horizontal & altitude AGL — sensor kunci proyek |
| Flight controller | ArduPilot-compatible: Pixhawk 6C / Matek H743 / Cube | Menjalankan EKF3 & mission engine; pastikan UART bebas untuk MTF-01P |
| Kompas eksternal | Magnetometer pada tiang (mast), menjauh dari ESC/motor | Sumber yaw absolut (EK3_SRC1_YAW=1) — kualitas heading sangat menentukan drift |
| Modul GPS | GNSS standar (mis. M10) | HANYA ground truth logging untuk analisis drift; nonaktif di EKF |
| Frame quadcopter | Kelas 5–7 inci riset, ruang mounting sensor bawah | Sensor flow harus menghadap tanah tanpa halangan |
| Motor + ESC + propeler | Sesuai kelas frame; ESC BLHeli/AM32 | Sediakan propeler cadangan untuk fase uji terbang |
| Baterai LiPo + charger | 4S–6S sesuai powertrain; charger balance; ≥3 unit baterai | Fire-safe bag wajib untuk penyimpanan & pengisian |
| Remote control + receiver | Mis. ELRS/Crossfire | Failsafe manual — wajib ada di semua uji terbang |
| Telemetry link | Radio telemetry / WiFi | Link MAVLink drone ↔ QGC/laptop lapangan |
| Perkakas bench | Solder + timah, multimeter, hex driver set, tie-strap, heat-shrink | Perakitan & troubleshooting (fase 3, mitra elektro) |
| Area & perlengkapan uji | Lapangan/indoor bertekstur, pencahayaan cukup; P3K | Optical flow butuh permukaan bertekstur; patuhi regulasi penerbangan setempat |

## 5. Pembagian Tugas Tim

| Fase | Gabriel (Informatika) | Mitra (Teknik Elektro) |
|----|----|----|
| 1–2 | Setup lingkungan, SITL, skrip misi, pipeline analisis drift | Studi datasheet sensor, perencanaan wiring & powertrain, pengadaan komponen |
| 3 | Validasi data sensor (analisis log bench test), konfigurasi parameter | Perakitan, wiring, kalibrasi, jaminan kualitas sinyal sensor |
| 4 | Perencanaan misi uji, logging, analisis drift | Kesiapan & keselamatan drone di lapangan, pilot failsafe, perbaikan hardware |
| 5 | Seluruh modifikasi QGC (QML + C++), dokumentasi arsitektur | Dukungan pengujian hardware paralel, masukan kebutuhan tampilan sensor |
| 6 | Uji terpadu, analisis final, penulisan naskah (first author bagian software/algoritma) | Uji terpadu sisi hardware, penulisan bagian sistem/elektronika |

## 6. Metrik Keberhasilan & Milestone Paper

- Return-to-home error (meter): jarak antara posisi akhir drone dan titik home menurut ground truth, setelah misi berpola diketahui.

- Drift per meter tempuh (m/m): normalisasi error terhadap total jarak misi — memungkinkan perbandingan antar-misi dan antar-paper.

- Konsistensi antar-run: standar deviasi metrik pada ≥10 pengulangan; menunjukkan reliabilitas, bukan sekadar keberhasilan sekali.

- Perbandingan SITL vs dunia nyata: seberapa baik simulasi memprediksi perilaku nyata — kontribusi metodologis untuk paper.

Milestone data untuk paper: (M1) dataset drift SITL — akhir Fase 2; (M2) laporan kualitas sensor — akhir Fase 3; (M3) dataset drift dunia nyata — akhir Fase 4; (M4) demonstrasi end-to-end QGC modifikasi + drone nyata — Fase 6; (M5) submit naskah — akhir Fase 6.

## 7. Risiko & Mitigasi

| Risiko | Dampak | Mitigasi |
|----|----|----|
| Kualitas optical flow buruk (permukaan minim tekstur / cahaya kurang) | Estimasi velocity tidak andal → drift besar / EKF failsafe | Pilih area uji bertekstur; pantau flow quality di panel sensor; uji variasi permukaan secara sistematis (jadi data paper) |
| Interferensi magnetik pada kompas | Heading salah → seluruh navigasi melenceng | Kompas pada mast, kalibrasi menyeluruh, bench test dengan motor berputar |
| QGC master berubah cepat (moving target) | Konflik merge, build rusak, hasil sulit direproduksi | Kunci commit hash basis; kerja di branch sendiri; catat konfigurasi build lengkap |
| Kerusakan drone saat uji terbang | Jadwal mundur, biaya komponen | Uji bertahap (hover dulu), pilot failsafe manual, propeler & spare part cadangan |
| Beban ganda (Fase 4 & 5 paralel) | Kelelahan, kualitas menurun | Pembagian tugas tegas (Fase 4 dipimpin mitra elektro, Fase 5 fokus Gabriel); review mingguan |
