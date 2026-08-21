# Prosedur Simulasi Penerbangan Tanpa GPS (SITL) — v1

> Basis: ArduPilot master (4.6.0-beta1-dev), QGC master (branch `gps-denied-mission`,
> basis `22173690e`). Prosedur terbang diturunkan dari autotest resmi ArduPilot
> (`Tools/autotest/arducopter.py`: `OpticalFlowLimits`,
> `configure_EKFs_to_use_optical_flow_instead_of_GPS`).
> Dibuat 2026-07-16.

## Tahap 0 — Menjalankan dunia

```bash
# Terminal 1 — SITL (JANGAN pakai flag -w: itu menghapus parameter tersimpan)
cd /Users/mc/CLionProjects/ardupilot
source .venv/bin/activate
Tools/autotest/sim_vehicle.py -v ArduCopter --console --map
```

```bash
# Terminal 2 — QGC
open /Users/mc/CLionProjects/qgroundcontrol/cmake-build-debug/QGroundControl.app
```

## Tahap 1 — Parameter (sekali set, persisten antar-run)

Di QGC: **Vehicle Setup → Parameters → Search**. Untuk tiap parameter: klik →
ubah nilai → **Save/Enter** → pastikan nilai baru benar-benar tampil di daftar
(jebakan umum: nilai diketik tapi tidak tersimpan).

| # | Parameter | Nilai | Fungsi |
|---|---|---|---|
| 1 | `SIM_FLOW_ENABLE` | 1 | Simulator menghidupkan sensor flow virtual |
| 2 | `FLOW_TYPE` | 10 | Driver optical flow = SITL |
| 3 | `RNGFND1_TYPE` | 100 | Driver rangefinder = SITL |
| 4 | `RNGFND1_MAX` | 12 | Jangkauan 12 **meter** (master pakai meter, BUKAN cm) |
| 5 | `SIM_GPS1_ENABLE` | 0 | Simulator berhenti memproduksi GPS |
| 6 | `GPS1_TYPE` | 0 | Driver GPS mati total |
| 7 | `EK3_SRC1_POSXY` | 0 | Tanpa sumber posisi horizontal absolut (dead reckoning) |
| 8 | `EK3_SRC1_VELXY` | 5 | Velocity horizontal = optical flow |
| 9 | `EK3_SRC1_POSZ` | 2 | Ketinggian = rangefinder (fallback: 1 = baro) |
| 10 | `EK3_SRC1_VELZ` | 0 | Tanpa sumber velocity vertikal (default 3=GPS → wajib 0) |
| 11 | `EK3_SRC1_YAW` | 1 | Heading = kompas |

Nilai #7, #8, #10 identik dengan helper resmi autotest
`configure_EKFs_to_use_optical_flow_instead_of_GPS()` (arducopter.py:3590).

Setelah SEMUA ter-set: **reboot** (halaman Parameters → Tools → Reboot Vehicle,
atau ketik `reboot` di MAVProxy). Parameter driver (#1–3, #6) hanya berefek
saat boot.

## Tahap 2 — GATE verifikasi (wajib lulus sebelum terbang)

1. **Spot-check parameter** (search ulang di QGC):
   `GPS1_TYPE=0 · SIM_GPS1_ENABLE=0 · RNGFND1_MAX=12 · EK3_SRC1_POSXY=0 ·
   EK3_SRC1_VELXY=5 · EK3_SRC1_VELZ=0`
2. **MAVLink Inspector**:
   - `GPS_RAW_INT`: hilang, atau `fix_type=0` dan `satellites_visible=0`
   - `OPTICAL_FLOW`: mengalir, `quality` > 0
   - `DISTANCE_SENSOR`: `current_distance` ≈ 24 (cm, drone di darat — normal),
     `max_distance` = **1200** (bukti RNGFND1_MAX=12 berlaku)
   - `LOCAL_POSITION_NED`: tetap hidup
3. **Console MAVProxy**: tunggu `EKF3 IMU0 initialised` / `EKF3 active`.
   Pesan PreArm yang muncul DICATAT kata-per-kata (jangan langsung diperbaiki).

## Tahap 3 — Skenario terbang pertama (pola resmi ArduPilot)

**KENAPA takeoff di ALT_HOLD, bukan LOITER/GUIDED**: di darat optical flow belum
"sehat" (terlalu dekat tanah, belum ada gerakan visual) → mode berbasis posisi
menolak arming. ALT_HOLD hanya butuh attitude + ketinggian. Setelah mengudara,
flow sehat → pindah LOITER. (Sumber: arducopter.py:3701 — "we can't takeoff in
loiter as we need flow healthy".)

Semua perintah di console MAVProxy:

```
mode alt_hold        # mode takeoff non-posisi
rc 3 1000            # throttle rendah (syarat arming)
arm throttle
rc 3 1800            # naik
                     # pantau ketinggian (VFR_HUD.alt / DISTANCE_SENSOR)
rc 3 1500            # tahan di ~5 m
mode loiter          # flow sudah sehat -> posisi terkunci
```

**Maju/mundur/belok** (RC netral = 1500; offset ±100–200 = pelan):

```
rc 2 1400            # MAJU  (pitch ke depan)
rc 2 1500            # berhenti
rc 2 1600            # MUNDUR
rc 1 1400 / 1600     # geser KIRI / KANAN (roll)
rc 4 1450 / 1550     # putar KIRI / KANAN (yaw)
rc 4 1500            # berhenti berputar
```

**Mendarat**: semua channel ke netral (`rc 1 1500`, `rc 2 1500`, `rc 4 1500`)
lalu `mode land`. Auto-disarm setelah menyentuh tanah.

> ⚠️ **JANGAN pakai RTL di konfigurasi non-GPS**: tanpa GPS tidak ada home
> position yang valid — RTL tidak tahu harus pulang ke mana. Selalu LAND.
> (Kebiasaan ini dibawa sampai uji terbang nyata Fase 4.)

**Yang diamati selama terbang** (bahan laporan):
- `LOCAL_POSITION_NED`: x/y/vx/vy mengikuti gerakan → inilah data Local Grid View
- `OPTICAL_FLOW.quality` saat diam vs bergerak
- `EKF_STATUS_REPORT`: `velocity_variance` / `pos_horiz_variance` saat manuver
- Peta QGC: apakah ikon drone tampil/bergerak? (tanpa GPS kemungkinan tidak —
  bukti kebutuhan Local Grid View)

## Tahap 4 — Simpan artefak

1. QGC Parameters → **Tools → Save to file** →
   `config/gps-denied-sitl-verified.params` (snapshot konfigurasi terverifikasi).
2. Commit ke git proyek misi bersama dokumen ini.

## Catatan untuk tahap berikutnya (mission script)

- Mode GUIDED + `SET_POSITION_TARGET_LOCAL_NED` (misi otonom) kemungkinan butuh
  **EKF origin** di-set dulu via pesan `SET_GPS_GLOBAL_ORIGIN` — pola resmi ada
  di autotest `set_origin()` (arducopter.py:4438). Ini tugas `mission_runner.py`
  (pymavlink), bukan konfigurasi parameter.
- Tanpa sumber posisi absolut, posisi = integrasi velocity → **drift pasti
  terjadi dan itu DATA riset**, bukan bug.

## Troubleshooting

| Gejala | Diagnosis | Tindakan |
|---|---|---|
| PreArm "Need Position Estimate" saat coba LOITER di darat | Flow belum sehat — by design | Takeoff ALT_HOLD (prosedur di atas) |
| Arming ditolak di ALT_HOLD | Baca pesan; umumnya throttle belum rendah | `rc 3 1000` sebelum `arm throttle` |
| `OPTICAL_FLOW.quality` = 0 terus | Driver flow tidak jalan | Cek param #1–2, reboot |
| Ketinggian melayang/aneh | Sumber POSZ | Coba fallback `EK3_SRC1_POSZ=1` (baro) |
| GPS masih tampil fix | Param #5–6 tidak tersimpan | Set ulang, Save, reboot, cek lagi |
