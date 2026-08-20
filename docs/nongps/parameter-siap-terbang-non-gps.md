# Panduan Parameter Siap Terbang Tanpa GPS

> **Wahana**: Pixhawk 6C + MicoAir MTF-01P (optical flow + lidar, MAVLink di TELEM2).
> **Firmware**: ArduCopter 4.7.0.
> **Basis verifikasi**: setiap nilai di dokumen ini dicek ke source pada tag `Copter-4.7.0`
> di checkout lokal `/Users/mc/CLionProjects/ardupilot`, bukan ke dokumentasi umum atau tutorial.
> Dokumen pendamping: `konfigurasi-parameter-non-gps.html` (jalur pesan/telemetri),
> `prosedur-terbang-non-gps-sitl.md` (prosedur terbang).

## 0. Cara memakai panduan ini

Tiap tabel punya kolom **Verifikasi**: cara membuktikan nilai itu benar-benar bekerja, bukan
sekadar tersimpan. Ada tiga tingkat bukti, dan ketiganya dipakai:

- **S** — bukti dari source. Perintah untuk membaca ulang sendiri ada di [Lampiran A](#lampiran-a--cara-membaca-ulang-source).
- **T** — bukti dari telemetri saat terhubung (QGC → Analyze → MAVLink Inspector).
- **L** — bukti dari log `.bin` setelah terbang (QGC → Analyze → Log Download).

Aturan kerja:

1. **Catat dulu, ubah kemudian.** QGC → Vehicle Setup → Parameters → Tools → *Save to file*,
   simpan sebagai `pra-<tanggal>.params`. Setelah selesai, simpan lagi dan `diff` keduanya —
   itulah bukti perubahan yang benar-benar terjadi.
2. **Satu kelompok per sesi.** Kalau digabung, kamu kehilangan kemampuan menyimpulkan mana yang bekerja.
3. Parameter bertanda **R** butuh **reboot** untuk berlaku.
4. Nilai yang diketik belum tentu tersimpan — cari ulang parameternya dan pastikan nilai baru tampil.

---

## 1. Rantai estimasi posisi (EKF3)

Ini inti konfigurasi non-GPS: memberi tahu EKF3 dari mana tiap komponen state berasal.

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `AHRS_EKF_TYPE` | `3` | EKF3, satu-satunya estimator yang mendukung sumber optical flow | **T**: `EKF_STATUS_REPORT` mengalir; console "EKF3 IMU0 initialised" |
| `EK3_ENABLE` | `1` | EKF3 aktif | **T**: sama seperti di atas |
| `EK3_SRC1_POSXY` | `0` (None) | Tidak ada sumber posisi horizontal absolut. Nilai 5 tidak ada untuk parameter ini — flow hanya sah sebagai sumber *velocity* | **S**: `AP_NavEKF_Source.cpp` `@Values: 0:None, 3:GPS, 4:Beacon, 6:ExternalNav` |
| `EK3_SRC1_VELXY` | `5` (OpticalFlow) | Kecepatan horizontal dari flow; posisi = integrasinya | **S**: `@Values: ... 5:OpticalFlow ...`; **L**: `XKF5.FIX/FIY` ≠ 0 |
| `EK3_SRC1_POSZ` | `1` (Baro) | **Sengaja Baro, bukan RangeFinder.** Rangefinder mengambil alih otomatis di bawah ambang `EK3_RNG_USE_HGT`, dan mekanisme itu hanya bekerja bila sumber utama Baro atau GPS | **S**: deskripsi `EK3_RNG_USE_HGT`: "…when below this percentage of its maximum range **and the primary height source is Baro or GPS**" |
| `EK3_SRC1_VELZ` | `0` (None) | Default 3 (GPS) harus dinolkan, tidak ada sumber velocity vertikal | **S**: `@Values: 0:None, 3:GPS, 4:Beacon, 6:ExternalNav` |
| `EK3_SRC1_YAW` | `1` (Compass) | Heading absolut satu-satunya yang tersedia | **S**: `@Values: 0:None, 1:Compass, 2:GPS, …` |
| `EK3_SRC2_*`, `EK3_SRC3_*` | sama persis dengan SRC1 | Tidak ada saklar RC opsi 90 (EKF Source Set), jadi set 2 dan 3 tidak pernah dipilih. Menyamakan mencegah perilaku tak terduga bila suatu saat terpicu | **S**: cek tidak ada `RCx_OPTION = 90` |
| `EK3_SRC_OPTIONS` | `0` | Bit 0 (FuseAllVelocities) tidak relevan tanpa sumber velocity kedua | **S**: `@Bitmask: 0:FuseAllVelocities, 1:AlignExtNavPosWhenUsingOptFlow, 3:UsePerCoreEKFSources` |
| `EK3_FLOW_USE` | `1` (Navigation) **R** | Flow masuk ke navigation filter 24-state, bukan hanya estimator ketinggian terrain | **S**: `@Values: 0:None,1:Navigation,2:Terrain`, `@RebootRequired: True` |
| `EK3_RNG_USE_HGT` | `70` | Persen dari `RNGFND1_MAX`. 70 adalah maksimum yang diizinkan | **S**: `@Range: -1 70` |
| `EK3_RNG_USE_SPD` | `2` | Rangefinder berhenti jadi sumber ketinggian di atas kecepatan ini | **S**: `@Range: 2.0 6.0`, `@Units: m/s` |
| `EK3_OGN_HGT_MASK` | `0` | Koreksi datum hanya bermakna bila ada tinggi GPS | **S**: deskripsi param, "only operates when GPS quality permits" |
| `EK3_MAG_CAL` | `3` (default Copter) | Heading fusion di darat, 3-axis setelah reset yaw pertama di udara | **S**: `MAG_CAL_DEFAULT 3` untuk Copter |

**Yang harus kamu pahami dari #POSZ**: dengan `POSZ=1` + `RNG_USE_HGT=70` + `RNGFND1_MAX=8`,
rangefinder jadi sumber ketinggian **hanya di bawah 5,6 m dan di bawah 2 m/s**. Di luar amplop itu
ketinggian kembali ke baro. Lompatan ketinggian saat melewati batas itu adalah perilaku desain,
bukan kerusakan — dan itulah alasan Bagian 4 membatasi kecepatan.

---

## 2. Sensor

### 2.1 Optical flow

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `FLOW_TYPE` | `5` (MAVLink) **R** | MTF-01P bicara MAVLink lewat TELEM2 | **S**: `@Values: … 5:MAVLink …`; **T**: pesan `OPTICAL_FLOW` muncul |
| `FLOW_ORIENT_YAW` | `18000` | Sensor terpasang berputar 180°. Satuannya **centi-derajat** | **S**: `@Units: cdeg`, `@Range: -17999 +18000` |
| `FLOW_FXSCALER` | `-30` | Hasil kalibrasi. Tiap 1 poin = 0,1% skala | **S**: `@Range: -800 +800`; **L**: `OF.flowX` vs gerakan nyata |
| `FLOW_FYSCALER` | `-24` | idem sumbu Y | idem |
| `FLOW_POS_X/Y/Z` | offset fisik sebenarnya (m) | Jarak lensa flow ke titik acuan badan. Z yang salah = **kesalahan skala kecepatan**, bukan sekadar derau. Prosedur pengisian: [Lampiran C](#lampiran-c--mengisi-offset-posisi-sensor-flow_pos_-rngfnd1_pos_) | **S**: `@Units: m`; `AP_NavEKF3_OptFlowFusion.cpp:314,331` |

> **Kalibrasi flow wajib diulang** setiap sensor dilepas, dipindah, atau lensanya tergeser.
> `FXSCALER/FYSCALER` adalah properti pemasangan, bukan properti wahana.

### 2.2 Rangefinder

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `RNGFND1_TYPE` | `10` (MAVLink) **R** | Lidar MTF-01P lewat jalur yang sama | **S**: `@Values: … 10:MAVLink …`; **T**: `DISTANCE_SENSOR` mengalir |
| `RNGFND1_ORIENT` | `25` (Down) | Menghadap ke bawah | **S**: `@Values: … 25:Down` |
| `RNGFND1_MIN` | **cek datasheet** (default firmware 0.20) | Nilaimu sekarang `0.02` — kalau itu bukan angka datasheet MTF-01P, pembacaan derau di dekat tanah akan dianggap sah | **S**: `@Units: m`, default `0.20`; **T**: `DISTANCE_SENSOR.min_distance` |
| `RNGFND1_MAX` | `8` | Konservatif terhadap jangkauan 12 m sensor. Menentukan amplop `EK3_RNG_USE_HGT` | **S**: `@Units: m`, default `7.00`; **T**: `DISTANCE_SENSOR.max_distance` = **800** (cm) |
| `RNGFND1_POS_X/Y/Z` | offset fisik sebenarnya (m) | Diukur ke jendela ToF, bukan ke lensa flow — di MTF-01P keduanya berbeda beberapa cm. Lihat [Lampiran C](#lampiran-c--mengisi-offset-posisi-sensor-flow_pos_-rngfnd1_pos_) | **S**: `@Units: m`; `AP_NavEKF3_PosVelFusion.cpp:1196` |

### 2.3 Kompas — satu-satunya sumber heading

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `COMPASS_USE` | `1` | Kompas dipakai untuk yaw | **S**: `@Values: 0:Disabled,1:Enabled` |
| `COMPASS_AUTODEC` | **`0`** (dari 1) | Deklinasi otomatis dihitung **dari lokasi GPS**. Tanpa GPS, fungsi ini keluar lebih awal dan deklinasi tidak pernah terisi | **S**: `Compass::try_set_initial_location()` — `if (!_auto_declination) return; … if (!AP::ahrs().get_location(loc)) return;` |
| `COMPASS_DEC` | deklinasi lokasimu, **dalam radian** | Tanpa ini, "utara" wahanamu adalah utara magnetik, bukan utara sejati — bias sistematis di seluruh data drift | **S**: `@Units: rad`, `@Range: -3.142 3.142`. Konversi: `rad = derajat × π/180` |
| `COMPASS_LEARN` | `0` | Learning in-flight melarang mode berbasis posisi, dan EKF-learning butuh estimasi yang justru sedang kita uji | **S**: `@Values: 0:Disabled,2:EKF-Learning,3:InFlight-Learning` |

> Kenapa `AUTODEC=0` penting justru untuk fase berikutnya: di Fase 4 GPS akan dipasang sebagai
> *ground truth*. Begitu ada lokasi, `try_set_initial_location()` akan **menimpa** `COMPASS_DEC`
> di RAM. Dengan `AUTODEC=0`, nilai kalibrasimu bertahan di kedua fase — dan itu syarat
> reproducibility antar-run.

---

## 3. GPS benar-benar mati

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `GPS1_TYPE` | `0` | Driver GPS mati total | **T**: `GPS_RAW_INT` hilang, atau `fix_type=0` dan `satellites_visible=0` |
| `GPS2_TYPE` | `0` | idem | idem |
| `AHRS_GPS_USE` | `0` | Hanya memengaruhi AHRS berbasis DCM; EKF3 memakai parameternya sendiri. Relevan nanti saat GPS dipasang sebagai ground truth | **S**: deskripsi `AHRS_GPS_USE`: "Currently this affects only the DCM-based AHRS: the EKF uses GPS according to its own parameters" |
| `ARMING_NEED_LOC` | `0` | Tidak menuntut posisi absolut untuk arming | **S**: `@Values{Copter,Rover}: 0:Do not require location,1:Require Location` |
| `SERIAL3_PROTOCOL`, `SERIAL4_PROTOCOL` | `-1` (opsional) | Kedua port GPS kosong. Tidak menggeser penomoran `MAVn_` karena bukan port MAVLink | **S**: `GCS::setup_uarts()` hanya memindai port ber-protokol MAVLink |

---

## 4. Amplop operasi — batas kecepatan mengikuti batas sensor

Ini bagian yang paling sering dilewatkan. Dua parameter EKF menetapkan amplop, lalu **parameter
kecepatan harus dibuat menghormatinya**, kalau tidak wahana keluar dari amplop tanpa peringatan.

```
Rangefinder jadi sumber ketinggian  ⟺  tinggi < 70% × 8 m = 5,6 m  DAN  kecepatan < 2 m/s
```

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `WP_SPD` | `2` atau kurang | Di atas `EK3_RNG_USE_SPD` ketinggian melompat ke baro di tengah misi | **S**: `@Units: m/s`, `@Range: 0.10 20.00` (4.7: `WPNAV_SPEED` cm/s → `WP_SPD` **m/s**) |
| `WP_SPD_UP` | `1` | Naik pelan supaya flow tetap terkunci | **S**: `AC_WPNav.cpp` `SPD_UP` |
| `WP_SPD_DN` | `0.5` | Turun pelan, terutama di bawah 1 m | **S**: `AC_WPNav.cpp` `SPD_DN` |
| `WP_RADIUS_M` | `0.5`–`1` | Radius waypoint dalam **meter** di 4.7 | **S**: `@Units: m`, `@Range: 0.05 10.00` |
| `LOIT_SPEED_MS` | `2` atau kurang | Alasan sama dengan `WP_SPD`; satuan m/s di 4.7 | **S**: `AC_Loiter.cpp` `SPEED_MS`, `@Units: m/s` |
| `PILOT_SPD_UP` | `1.5`–`2.5` | Satuan **m/s** di 4.7 (dulu `PILOT_SPEED_UP` cm/s) | **S**: `@Units: m/s`; konversi param tercatat di `Parameters.cpp` |
| `PILOT_SPD_DN` | `1` | 0 berarti "ikuti PILOT_SPD_UP" — terlalu cepat untuk turun di atas flow | **S**: "If 0 PILOT_SPD_UP value is used" |
| `WP_RFND_USE` | **`0`** (dari 1) | Terrain following pakai rangefinder **bertentangan** dengan `EK3_RNG_USE_HGT`. Dokumentasi EK3 menyatakan fitur itu bukan untuk terrain following | **S**: deskripsi `EK3_RNG_USE_HGT`: "This feature should not be used for terrain following…" |
| `SURFTRAK_MODE` | `1` (Ground) | Surface tracking di AltHold/Loiter memakai rangefinder yang sama; default dan sesuai | **S**: `@Values: 0:Do not track, 1:Ground, 2:Ceiling` |
| `MOT_HOVER_LEARN` | `2` (Learn and Save) | Throttle hover yang akurat = AltHold stabil = flow lebih tenang | **S**: `@Values: 0:Disabled, 1:Learn, 2:Learn and Save` |

---

## 5. Arming

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `ARMING_SKIPCHK` | `0` | Tidak ada check yang dilewati. **Jangan `-1`** | **S**: `@Bitmask: 1:Barometer,2:Compass,3:GPS lock,4:INS,…` (yang di-skip, bukan yang aktif) |
| `ARMING_OPTIONS` | `0` | Bit 0 membungkam laporan prearm; bit 1 membungkam statustext Armed/Disarmed | **S**: `@Bitmask: 0:Disable prearm display,1:Do not send status text on state change,2:Skip IMU consistency checks when ICE motor running` |
| `ARMING_MIS_ITEMS` | `0` | Tidak menuntut item misi tertentu | **S**: `@Bitmask: 0:Land,1:VTOL Land,2:DO_LAND_START,3:Takeoff,…` |
| `ARMING_ACCTHRESH` | `0.75` (default) | Ambang konsistensi akselerometer | **S**: `AP_Arming.cpp` `ins_accels_consistent()` |
| `ARMING_MAGTHRESH` | `100` (default, biarkan) | **Check ini tidak akan pernah menyala di wahanamu** — ia dibungkus `ahrs.get_location()`, yang selalu gagal tanpa posisi absolut | **S**: `AP_Arming::compass_checks()` — `if ((magfield_error_threshold > 0) && ahrs.use_compass() && ahrs.get_location(ahrs_loc))` |

---

## 6. Failsafe — semuanya harus bermuara ke LAND

Aturan tunggal untuk wahana ini: **tidak ada RTL, tidak ada SmartRTL**. Tanpa posisi absolut,
"pulang" tidak punya arti — dan home yang dihitung dari integrasi flow ikut hanyut.

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `FS_THR_ENABLE` | `3` (Enabled always Land) | RC hilang → mendarat di tempat | **S**: `@Values: 0:Disabled,1:Enabled always RTL,…,3:Enabled always Land,…` |
| `FS_THR_VALUE` | `975` | Harus di atas 910 dan di bawah `RC3_MIN`−10, kalau tidak muncul `PreArm: Check FS_THR_VALUE` | **S**: `AP_Arming_Copter::parameter_checks()` |
| `FS_EKF_ACTION` | `1` | Pindah ke Land bila mode saat itu butuh posisi | **S**: `@Values: 0:Report only,1:Switch to Land mode if current mode requires position,2:Switch to AltHold…,3:Switch to Land from all modes` |
| `FS_EKF_THRESH` | `0.8` (Default) | Ambang variance untuk arming check dan EKF failsafe | **S**: `@Values: 0:Disabled, 0.6:Strict, 0.8:Default, 1.0:Relaxed` |
| `FS_DR_ENABLE` | **`1` (Land)** (dari 2) | Nilai 2 = RTL, tidak masuk akal di sini. Praktis tak pernah terpicu di copter, tapi konfigurasi tidak boleh menyesatkan pembaca berikutnya | **S**: `@Values: 0:Disabled/NoAction,1:Land,2:RTL,…` |
| `FS_DR_TIMEOUT` | `30` (default) | Detik dead-reckoning sebelum EKF failsafe menyusul | **S**: `ParametersG2` `FS_DR_TIMEOUT` |
| `FS_VIBE_ENABLE` | `1` | Kompensasi vibrasi; flow sensitif terhadap getaran | **S**: `@Values: 0:Disabled, 1:Enabled` |
| `FS_CRASH_CHECK` | `1` | Disarm otomatis saat crash terdeteksi | **S**: `@Values: 0:Disabled, 1:Enabled` |
| `FS_GCS_ENABLE` | `0` selama pilot memegang RC; **`5` (Land)** saat misi otonom dijalankan dari `mission_runner.py` | Saat setpoint datang dari GCS/companion, putusnya link = tidak ada lagi yang memerintah. Jangan pernah 1/3/4/6 (semuanya RTL/SmartRTL) | **S**: `@Values: 0:Disabled/NoAction,1:RTL,…,5:Land,…` |
| `FS_GCS_TIMEOUT` | `5` (default) | Detik sebelum GCS failsafe | **S**: `@Units: s` |
| `FS_OPTIONS` | `16` (default) | Bit 4 = lanjutkan mode kendali pilot saat GCS failsafe | **S**: `@Bitmask: …4:Continue if in pilot controlled modes on GCS failsafe,…` |
| `BATT_FS_LOW_ACT` | **`1` (Land)** | Default 0 = hanya peringatan | **S**: `@Values{Copter}: 0:Warn only,1:Land,2:RTL,…` |
| `BATT_FS_CRT_ACT` | **`1` (Land)** | idem | idem |
| `BATT_LOW_VOLT` / `BATT_CRT_VOLT` | sesuai pack | Harus diisi, kalau 0 aksi di atas tidak pernah terpicu | **T**: `BATTERY_STATUS` (stream EXTRA3) |
| `BATT_LOW_TIMER` | `10` (default) | Detik tegangan harus bertahan rendah sebelum failsafe | **S**: `AP_BattMonitor_Params` |

---

## 7. Fence yang benar-benar bekerja tanpa posisi

Konfigurasi sekarang (`FENCE_TYPE = 7`, `FENCE_ENABLE = 0`) adalah fence yang mati **dan** akan
menuntut posisi kalau dinyalakan. Fence ketinggian tidak menuntut posisi — dan itu satu-satunya
pagar yang bisa kamu miliki.

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `FENCE_ENABLE` | `1` | Pagar ketinggian aktif | **T**: `FENCE_STATUS` (stream EXT_STAT) |
| `FENCE_TYPE` | **`1`** (Max altitude saja, dari 7) | Bit 1 (Circle) dan bit 2 (Polygon) yang menuntut posisi | **S**: `AC_Fence::pre_arm_check()` — pemeriksaan posisi hanya untuk `AC_FENCE_TYPE_CIRCLE`/`POLYGON` |
| `FENCE_ALT_MAX` | `10` | Batas bawah rentang parameter. Amplop rangefinder (5,6 m) tetap dijaga lewat disiplin terbang, plafon ini pengaman terakhir | **S**: `@Units: m`, `@Range: 10 1000` |
| `FENCE_ALT_MAX_TP` | `1` (Above Home) — default, **jangan diubah ke 2** | Frame "Above Home" jatuh balik ke ketinggian barometrik saat home belum ada. Frame "Above Origin" mengembalikan gagal → **dianggap breach seketika** | **S**: `AC_Fence::get_alt_in_frame_m()` + `AP_AHRS::get_relative_position_D_home()` — "fall back to an altitude derived from barometric pressure" |
| `FENCE_ACTION` | **`2` (Always Land)** | Default 1 = RTL or Land | **S**: `@Values{Copter}: 0:Report Only,1:RTL or Land,2:Always Land,…` |
| `FENCE_MARGIN` | `2` (default) | Jarak peringatan sebelum breach | **S**: `@Units: m` |
| `FENCE_AUTOENABLE` | `0` (default) | Fence dikendalikan `FENCE_ENABLE` saja | **S**: default `ALWAYS_DISABLED` |
| `AVOID_ENABLE` | **`0`** (dari 3) | Bit 1 (proximity) menyala padahal semua `PRXn_TYPE = 0`; avoidance horizontal juga tidak berlaku pada fence ketinggian | **S**: `@Bitmask: 0:UseFence,1:UseProximitySensor,2:UseBeaconFence` |

---

## 8. Jalur pesan dan telemetri

Rinciannya ada di `konfigurasi-parameter-non-gps.html`; yang wajib untuk "siap terbang":

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `MAV2_OPTIONS` | `0` **R** | Bit 1 menandai TELEM1 sebagai *private*, dan channel private dibuang dari daftar tujuan STATUSTEXT — pesan error hilang sementara telemetri terasa normal | **S**: `GCS::statustext_send_channel_mask()`; **L**: rekaman `MAV`, kolom `flags` bit 3 harus **padam** untuk `chan=1` |
| `MAV3_OPTIONS` | **`2`** **R** | Port sensor (TELEM2/COMM_2) justru **tempat yang benar** untuk bit private: paket MTF-01P tidak lagi diteruskan ke TELEM1. Data sensor tetap diproses lokal | **S**: `MAVLink_routing.cpp` — "don't ever forward data from a private channel"; jalur `process_locally` tetap true untuk pesan broadcast. Konsekuensi: pass-through konfigurasi sensor lewat QGC ikut mati |
| `MAV_TELEM_DELAY` | `0` | > 0 menahan telemetri di port non-USB selama N detik setelah boot — pesan awal boot hilang hanya di TELEM1 | **S**: `GCS_MAVLINK::telemetry_delayed()` |
| `MAV_OPTIONS` | `0` | Bit 0 membuat FC hanya menerima MAVLink dari rentang `MAV_GCS_SYSID`; salah setel = perintah QGC diabaikan diam-diam | **S**: `@Bitmask: 0:Accept MAVLink only from system IDs given by MAV_SYSID_GCS and MAV_SYSID_GCS_HI` |
| `MAV_GCS_SYSID` | `255` (default QGC) | Menentukan siapa yang dihitung sebagai GCS untuk failsafe | **T**: heartbeat QGC di MAVLink Inspector |
| `MAV2_EXTRA3` | jangan dinolkan | Di stream ini ada `BATTERY_STATUS`, `EKF_STATUS_REPORT`, `OPTICAL_FLOW`, `DISTANCE_SENSOR` | **T**: keempatnya harus mengalir |
| `MAV2_POSITION` | `3` | `LOCAL_POSITION_NED` — satu-satunya posisi yang berarti tanpa GPS, dan sumber data Local Grid View | **T**: `LOCAL_POSITION_NED` mengalir |

---

## 9. Logging untuk riset drift

| Parameter | Nilai | Alasan | Verifikasi |
|---|---|---|---|
| `LOG_BITMASK` | `180222` (bit 11 menyala) | Bit 11 = Optical Flow; tanpa itu rekaman `OF` tidak ada dan analisis drift kehilangan variabel utamanya | **S**: `@Bitmask: …11:Optical Flow…`; 180222 memang mengandung bit 11 |
| `LOG_DISARMED` | `1` hanya saat uji bench, `0` saat terbang | Merekam sesi diam mempercepat diagnosis sensor, tapi memenuhi kartu SD | **L**: ukuran file |

---

## 10. Mode terbang

Takeoff **selalu** di ALT_HOLD: di darat flow belum sehat (terlalu dekat tanah, belum ada gerakan
visual), jadi mode berbasis posisi menolak arming. Setelah mengudara dan flow sehat, pindah LOITER.

| Parameter | Nilai | Catatan |
|---|---|---|
| `FLTMODE1` | `2` (AltHold) | Mode takeoff |
| `FLTMODE2` | `5` (Loiter) | Setelah flow sehat |
| `FLTMODE6` | `9` (Land) | Selalu tersedia di ujung saklar |
| `SIMPLE` / `SUPER_SIMPLE` | `0` | Super Simple butuh bearing dari home |
| `INITIAL_MODE` | `2` (opsional) | Mode saat boot |

Nomor mode diverifikasi dari `@Values` parameter `INITIAL_MODE`:
`2:AltHold, 5:Loiter, 9:Land, 20:Guided_NoGPS, 22:FlowHold`.

---

## 11. Gate verifikasi sebelum terbang

Lulus semua, atau tidak terbang.

**A. Nilai benar-benar tersimpan**
Save to file, lalu `diff pra-<tgl>.params pasca-<tgl>.params`. Yang berubah harus persis
daftar di Bagian 12 — tidak kurang, tidak lebih.

**B. Sensor benar-benar bicara** (MAVLink Inspector)

| Pesan | Yang harus terlihat |
|---|---|
| `OPTICAL_FLOW` | mengalir, `quality` > 0 saat ada tekstur |
| `DISTANCE_SENSOR` | `max_distance` = 800 (bukti `RNGFND1_MAX=8` berlaku), `current_distance` wajar |
| `LOCAL_POSITION_NED` | hidup dan bergerak saat wahana digeser |
| `GPS_RAW_INT` | hilang, atau `fix_type=0` dan `satellites_visible=0` |
| `EKF_STATUS_REPORT` | `velocity_variance` dan `pos_horiz_variance` di bawah `FS_EKF_THRESH` |
| `SYS_STATUS` | dekode `present`/`enabled`/`health`; yang **enabled=1 health=0** adalah yang gagal |

**C. Prearm bersih**
Tunggu sampai 30 detik (laporan periodik) atau paksa `MAV_CMD_RUN_PREARM_CHECKS`. Catat setiap
pesan **kata per kata sebelum memperbaikinya** — itu data, bukan gangguan.

**D. Setelah terbang, dari log**

| Rekaman | Yang dibaca | Membuktikan |
|---|---|---|
| `XKFS` | `source_set` = 0 | SRC1 yang aktif, bukan SRC2/3 |
| `XKF5` | `FIX`, `FIY`, `normInnov` | Flow benar-benar **difusikan** EKF, bukan sekadar diterima |
| `OF` | `Qual`, `flowX/Y`, `bodyX/Y` | Kualitas flow selama manuver |
| `MAV` | `flags` (bit 3 private), `ss`, `tf`, `rxdp` | Jalur pesan sehat vs sesak |
| `MSG` | seluruh teks FC | Ada di sini tapi tidak di QGC = murni masalah pengiriman |

---

## 12. Delta dari konfigurasi sekarang

Ini daftar kerjanya. Selain baris ini, jangan sentuh apa pun.

| Parameter | Sekarang | Jadi | Reboot | Alasan singkat |
|---|---|---|---|---|
| `MAV2_OPTIONS` | `2` | `0` | ya | Membuka jalur STATUSTEXT di TELEM1 |
| `MAV3_OPTIONS` | `0` | `2` | ya | Menghentikan paket sensor diteruskan ke TELEM1 |
| `WP_RFND_USE` | `1` | `0` | — | Bertentangan dengan `EK3_RNG_USE_HGT` |
| `FS_DR_ENABLE` | `2` (RTL) | `1` (Land) | — | Tidak ada "pulang" tanpa posisi |
| `AVOID_ENABLE` | `3` | `0` | — | Tidak ada sensor proximity |
| `FENCE_TYPE` | `7` | `1` | — | Hanya pagar ketinggian yang bekerja tanpa posisi |
| `FENCE_ENABLE` | `0` | `1` | — | Plafon ketinggian aktif |
| `FENCE_ALT_MAX` | default | `10` | — | Plafon |
| `FENCE_ACTION` | default `1` | `2` | — | Always Land |
| `COMPASS_AUTODEC` | `1` | `0` | — | Deklinasi otomatis butuh GPS |
| `COMPASS_DEC` | `0` | deklinasi lokal (rad) | — | Menghapus bias utara magnetik |
| `BATT_FS_LOW_ACT` | `0` | `1` | — | Land, bukan sekadar peringatan |
| `BATT_FS_CRT_ACT` | `0` | `1` | — | idem |
| `BATT_LOW_VOLT` / `BATT_CRT_VOLT` | cek | sesuai pack | — | Tanpa ini aksi tak pernah terpicu |
| `RNGFND1_MIN` | `0.02` | angka datasheet | — | Derau dekat tanah jangan dianggap sah |
| `WP_SPD` | cek | `≤ 2` | — | Menghormati `EK3_RNG_USE_SPD` |
| `LOIT_SPEED_MS` | cek | `≤ 2` | — | idem |
| `PILOT_SPD_DN` | `0` | `1` | — | 0 berarti mengikuti kecepatan naik |
| `FLOW_POS_*`, `RNGFND1_POS_*` | cek | offset fisik | — | Salah isi = velocity terkontaminasi rotasi |

---

## 13. Yang sengaja TIDAK diubah

- `EK3_SRC1_POSZ = 1` — **bukan** 2. Dengan 2, rangefinder jadi sumber ketinggian di segala tinggi
  dan di atas 8 m kamu kehilangan acuan sama sekali.
- `EK3_MAG_CAL` — biarkan default Copter (3).
- `ARMING_MAGTHRESH` — tidak pernah menyala tanpa lokasi; mengubahnya hanya menciptakan ilusi kendali.
- `MAV2_PARAMS = 0` — memang default; QGC mengambil parameter lewat MAVFTP.
- `BRD_SER1_RTSCTS = 2` — auto, radio tanpa flow control terdeteksi sendiri.
- Seluruh PID/tuning (`ATC_*`, `PSC_*`) — di luar cakupan panduan navigasi ini.

---

## Lampiran A — Cara membaca ulang source

Semua klaim di dokumen ini bisa kamu periksa sendiri. Pola perintahnya:

```bash
git -C /Users/mc/CLionProjects/ardupilot show Copter-4.7.0:<path> | grep -n -B8 -A4 '<pola>'
```

| Yang mau diperiksa | Path |
|---|---|
| `EK3_SRC*` | `libraries/AP_NavEKF/AP_NavEKF_Source.cpp` |
| `EK3_RNG_USE_HGT/SPD`, `EK3_FLOW_USE` | `libraries/AP_NavEKF3/AP_NavEKF3.cpp` |
| `FLOW_*` | `libraries/AP_OpticalFlow/AP_OpticalFlow.cpp` |
| `RNGFND1_*` | `libraries/AP_RangeFinder/AP_RangeFinder_Params.cpp` |
| `COMPASS_*` | `libraries/AP_Compass/AP_Compass.cpp` |
| `ARMING_*`, pesan PreArm generik | `libraries/AP_Arming/AP_Arming.cpp` |
| Pesan PreArm khusus Copter | `ArduCopter/AP_Arming_Copter.cpp` |
| `FS_*`, `LOG_BITMASK`, `SIMPLE`, `FLTMODE` | `ArduCopter/Parameters.cpp` |
| `FENCE_*` | `libraries/AC_Fence/AC_Fence.cpp` |
| `WP_*`, `LOIT_*` | `libraries/AC_WPNav/AC_WPNav.cpp`, `AC_Loiter.cpp` |
| `MAVn_OPTIONS` | `libraries/GCS_MAVLink/GCS_MAVLink_Parameters.cpp` |
| `MAV_*`, mask STATUSTEXT | `libraries/GCS_MAVLink/GCS.cpp` |
| Routing channel private | `libraries/GCS_MAVLink/MAVLink_routing.cpp` |
| Rekaman log EKF (`XKFS`, `XKF5`) | `libraries/AP_NavEKF3/AP_NavEKF3_Logging.cpp` |

## Lampiran B — Nama parameter yang berubah di 4.7

Tutorial non-GPS di internet umumnya ditulis untuk 4.3–4.5 dan akan menyesatkan:

| Lama | Baru di 4.7 | Catatan |
|---|---|---|
| `ARMING_CHECK` | `ARMING_SKIPCHK` | Logika **terbalik**: bitmask check yang *dilewati* |
| `SRn_*` | `MAVn_*` | Penomoran mengikuti urutan port MAVLink, bukan nomor SERIAL |
| `SYSID_MYGCS` | `MAV_GCS_SYSID` | |
| `TELEM_DELAY` | `MAV_TELEM_DELAY` | |
| `WPNAV_SPEED` (cm/s) | `WP_SPD` (m/s) | Satuan ikut berubah |
| `WPNAV_RADIUS` (cm) | `WP_RADIUS_M` (m) | |
| `LOIT_SPEED` (cm/s) | `LOIT_SPEED_MS` (m/s) | |
| `PILOT_SPEED_UP` (cm/s) | `PILOT_SPD_UP` (m/s) | |
| `RNGFNDx_MIN_CM/MAX_CM` | `RNGFNDx_MIN/MAX` (m) | |
| `SERIALn_OPTIONS` bit 10 (private) | `MAVn_OPTIONS` bit 1 | Bit lama dinolkan saat konversi, jejaknya lenyap |

## Lampiran C — Mengisi offset posisi sensor (`FLOW_POS_*`, `RNGFND1_POS_*`)

### Titik acuannya apa?

Bebas kamu pilih — **yang dipakai EKF hanyalah selisihnya terhadap IMU**:

```cpp
// AP_NavEKF3_OptFlowFusion.cpp:314
Vector3F posOffsetBody = ofDataDelayed.body_offset - accelPosOffset;   // FLOW_POS - INS_POS
// AP_NavEKF3_PosVelFusion.cpp:1196
Vector3F posOffsetBody = sensor->get_pos_offset() - accelPosOffset;    // RNGFND1_POS - INS_POS
```

`accelPosOffset` = `INS_POSn` dari IMU yang sedang aktif
(`AP_NavEKF3_Measurements.cpp:441`). Jadi cara paling sederhana yang tetap benar:

> **Biarkan `INS_POS*` = 0,0,0 dan ukur semua offset lain dari titik tengah flight controller.**

Ini disahkan oleh dokumentasi parameternya sendiri: "*If the IMU cannot be moved and velocity noise
is a problem, a location closer to the IMU can be used as the body frame origin*"
(`INS_POS_Z`). Syarat mutlaknya cuma satu: **satu titik acuan untuk semua** — `INS_POS`,
`FLOW_POS`, `RNGFND1_POS`, dan nanti `GPS_POS` saat GPS ground-truth dipasang.

### Sumbu dan tanda

| Sumbu | Positif ke arah |
|---|---|
| X | depan wahana |
| Y | kanan wahana |
| Z | **bawah** |

Satuan **meter** (`0.06`, bukan `6`). Dinyatakan dalam **body frame wahana**, bukan frame sensor —
`FLOW_ORIENT_YAW = 18000` (sensor terpasang terbalik 180°) **tidak** membalik tanda offset ini.
Ini jebakan yang mudah terjadi di wahanamu.

Yang diukur adalah **titik fokus lensa**: lensa kamera flow untuk `FLOW_POS_*`, dan jendela ToF
untuk `RNGFND1_POS_*`. Di MTF-01P keduanya lensa yang berbeda dan berjarak beberapa sentimeter —
jadi kedua kelompok parameter ini boleh berbeda nilainya.

### Cara mengukur (10 menit, di meja)

1. Wahana di permukaan rata dan datar, tanpa baling-baling.
2. Tandai acuan: titik tengah flight controller (presisi 1 cm sudah lebih dari cukup).
3. **X dan Y**: jatuhkan garis tegak lurus (unting-unting atau penyiku) dari titik tengah FC dan
   dari lensa sensor ke permukaan meja, lalu ukur jarak antar dua tanda itu. Sensor lebih ke depan
   dari FC → X positif; lebih ke kanan → Y positif.
4. **Z**: ukur beda tinggi bidang FC ke bidang lensa. Sensor di bawah FC → **Z positif**.
5. Bulatkan ke 0,01 m.

### Apa yang berubah kalau diisi

**Satu**, lengan gaya rotasi pada kecepatan (`AP_NavEKF3_OptFlowFusion.cpp:331`):

```cpp
relVelSensor = (prevTnb * stateStruct.velocity) + (ofDataDelayed.bodyRadXYZ % posOffsetBody);
```

Sensor yang tergeser dari IMU benar-benar ikut bergerak saat wahana berputar. Tanpa offset, gerakan
itu dibaca sebagai wahana yang bertranslasi.

**Dua**, jarak sensor ke tanah yang dipakai mengubah laju sudut flow menjadi kecepatan
(`AP_NavEKF3_OptFlowFusion.cpp:314–318`). Ini yang paling menentukan: kecepatan =
laju LOS × jarak, sehingga **kesalahan Z adalah kesalahan skala**, bukan sekadar derau.

**Tiga**, koreksi bacaan rangefinder ke posisi IMU (`AP_NavEKF3_PosVelFusion.cpp:1189–1203`),
dengan komentar sumber yang menyatakan maksudnya persis: "*the corrected reading is the reading
that would have been taken if the sensor was co-located with the IMU*".

### Kapan nol masih bisa diterima

| Komponen | Dampak kalau salah | Ambang layak diabaikan |
|---|---|---|
| X, Y | Kesalahan posisi ± sebesar offset setiap kali berputar. Terbatas, tidak tumbuh terhadap jarak: belok 90° menggeser sensor sejauh ≈ `r·√2` | ≤ 2 cm |
| Z | **Kesalahan skala kecepatan ≈ Δz / tinggi terbang.** Di hover 1,5 m, Z meleset 8 cm = skala meleset ~5% — pada misi kotak 20 m itu ~1 m, seorde dengan metrik return-to-home yang sedang kamu ukur | ≤ 2 cm, atau terbang jauh di atas 5 m (tidak berlaku untuk wahana ini) |

Artinya: kalau flight controller di pelat atas dan sensor di pelat bawah, **`FLOW_POS_Z` adalah
satu-satunya yang tidak boleh dibiarkan nol.**

### Urutan yang benar: offset dulu, kalibrasi flow kemudian

`FLOW_FXSCALER` / `FYSCALER` menskalakan laju flow, dan kecepatan = laju × jarak. Kalau jarak salah
karena `FLOW_POS_Z` masih nol, kalibrasi skala akan **menyerap** kesalahan itu — tapi hanya benar
di ketinggian tempat kalibrasi dilakukan, karena kesalahan Z bersifat penambahan tetap, bukan
perkalian. Nilaimu sekarang (`-30` / `-24`) dikalibrasi dengan offset nol, jadi:

1. Isi `FLOW_POS_*` dan `RNGFND1_POS_*`.
2. **Ulangi kalibrasi skala flow** setelahnya.

### Cara memverifikasi

| Uji | Cara | Yang membuktikan |
|---|---|---|
| Yaw di tempat | Hover ~1,5 m di LOITER, putar 360° pelan, amati `LOCAL_POSITION_NED` x/y | X/Y benar → posisi nyaris diam. Salah → posisi menyapu lingkaran berjari-jari ≈ offset |
| Skala jarak | Terbang lurus sepanjang jarak yang diukur pita ukur (mis. 10 m) di tinggi tetap | Selisih displacement vs pita ukur = sisa kesalahan skala; kalau muncul hanya di tinggi rendah, tersangkanya `FLOW_POS_Z` |
| Log | Bandingkan `XKF5.FIX/FIY` sebelum vs sesudah, kondisi terbang sama | Inovasi flow mengecil = model sensor lebih cocok dengan kenyataan |


---

*Disusun 19 Agustus 2026. Semua perilaku firmware diverifikasi terhadap source pada tag
`Copter-4.7.0` di checkout lokal, bukan dari dokumentasi umum.*
