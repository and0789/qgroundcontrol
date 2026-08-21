# Rencana Pembenahan UI Local Grid: Responsif, Sentuh, dan Paritas Pembuatan Misi

Status: Bagian 0–5a dan 6 selesai; 7 berikutnya; 5b/5c ditunda dan terhalang • Disusun 20 Agustus 2026 • Branch `feat/nongps-hud-overlay`

Dokumen ini menjawab tiga keluhan konkret: panel-panel di Local Grid saling bertabrakan dan tertutup
fitur lain di layar mobile, pembuatan waypoint di mode grid jauh lebih miskin dibanding halaman Plan,
dan aplikasi belum benar-benar responsif. Analisisnya di bawah menunjuk baris kode, bukan kesan.

---

## 1. Temuan Analisis

### 1.1 Kenapa panel bertabrakan

| # | Temuan | Bukti | Akibat yang terlihat |
|---|---|---|---|
| A1 | `LocalGridMissionActions` **tidak punya batas tinggi dan tidak bisa digulir**. Isinya kini 8 tombol + 9 label yang membungkus + progress bar, tingginya `implicitHeight` murni, dan ia diikat ke `bottom` sehingga tumbuh **ke atas** tanpa batas. | `LocalGridMissionActions.qml:60-61`, dipasang di `LocalGridView.qml:1594-1602` | Panel menjulur melewati tepi atas layar; tombol paling atas tidak bisa diraih. Ini penyebab utama yang Anda lihat. |
| A2 | Panel yang sama **hanya menghormati `leftEdgeBottomInset`**, padahal FlyView menerbitkan `leftEdgeTopInset` dan `leftEdgeCenterInset` untuk **tool strip** — yang letaknya persis di jalur tumbuh panel itu. | `LocalGridView.qml:1598`, sumber inset di `FlyViewWidgetLayer.qml:42-43` | Tabrakan dengan tool strip kiri. Inilah "tertutup oleh fitur lain". |
| A3 | Kolom kanan (readout + airspeed + mission list) memakai **lantai lebar, bukan plafon**: `Math.max(28 × fontWidth, readout.width)`. Bandingkan Plan view yang membatasi `Math.min(width / 3, 30 × fontWidth)`. | `LocalGridView.qml:1505` vs `PlanView.qml:23` | Di layar sempit kolom kanan memakan separuh lebih layar dan menabrak apa pun di kiri. |
| A4 | `LocalGridScaleBar` dan `LocalGridMissionActions` sama-sama diikat ke pojok kiri-bawah, dipisahkan angka ajaib `2.5 × fontHeight` yang menebak tinggi scale bar. | `LocalGridView.qml:1596-1609` | Begitu tinggi scale bar berubah atau inset joystick aktif, keduanya bertumpuk. |
| A5 | `LocalGridClickPanel.showAt()` menjepit posisi hanya ke `parent.width/height`, **tanpa sadar inset**, dan panel itu kini membawa dua label alasan multi-baris sehingga bisa lebih tinggi dari layar ponsel. | `LocalGridClickPanel.qml:120-122` | Panel muncul menimpa tool strip / panel instrumen, dan di layar pendek isinya terpotong. |
| A6 | Tidak ada penanganan **portrait** sama sekali. Seluruh overlay mengasumsikan bentuk lanskap: satu kolom kanan + tumpukan kiri-bawah. | struktur `LocalGridView.qml:1490-1649` | Di portrait kolom kanan dan tumpukan kiri bertemu di tengah. |

### 1.2 Kenapa uji di Windows belum menangkap semuanya

`--fake-mobile` **hanya membalik `ScreenTools.isMobile`**. Ia tidak mengubah `isTinyScreen` maupun
`isShortScreen`, karena keduanya dihitung dari ukuran fisik `Screen` (`ScreenTools.qml:96-97`), bukan
dari flag. Artinya yang Anda lihat di Windows adalah mode kegagalan **ukuran sentuh di dimensi
desktop**. Ponsel sungguhan menambah mode kedua: **sempit dan pendek**. Keduanya harus diuji, dan
yang kedua bisa direproduksi di desktop hanya dengan mengecilkan jendela.

```bash
./cmake-build-debug/QGroundControl.app/Contents/MacOS/QGroundControl --fake-mobile
```

### 1.3 Jarak fitur terhadap halaman Plan

Yang Plan punya dan grid belum. Kolom terakhir adalah penilaian jujur: sebagian memang **tidak
seharusnya** ditiru karena bergantung peta/GNSS.

| Kemampuan Plan | Grid sekarang | Bisa ditiru? |
|---|---|---|
| Sisip item **di posisi tertentu** (setelah item terpilih) | selalu menempel di akhir — `insertSimpleMissionItem(coordinate, -1)` | **Ya, murah.** API-nya sudah menerima `visualItemIndex` (`MissionController.h:112`) |
| Mode "senjata aktif": tombol Waypoint/ROI yang di-*toggle*, lalu klik peta menyisipkan | klik selalu membuka panel | **Ya** (`PlanView.qml:463-495`) |
| Tombol Takeoff / Land / Return di tool strip | hanya di dalam panel klik | **Ya** |
| **ROI / Cancel ROI** | tidak ada | **Ya** — `insertROIMissionItem` sudah tersedia (`MissionController.h:133`) |
| Pemilih **MAV_CMD lengkap** (kategori lanjutan) | hanya 3 jenis (`LocalGridMissionItemRow.qml:194`) | Ya, tapi berat — perlu dibatasi ke subset yang masuk akal tanpa GNSS |
| **Pola**: Survey / Corridor / Structure scan | tidak ada | Sebagian. Survey paling bernilai (pemetaan dalam ruangan), tapi butuh editor poligon di grid |
| **Mission settings** (item 0): cruise/hover speed, aksi akhir misi | tidak ada | **Ya** |
| **Statistik misi** (jarak, waktu, baterai) | tidak ada | **Ya** |
| Per item: hold time, acceptance radius, aksi kamera | tidak ada | Ya, sebagian |
| Frame altitude (relatif / AMSL / terrain) | hanya relatif | **Sengaja tidak.** Tanpa GNSS, AMSL dan terrain tidak punya arti; relatif-terhadap-rangefinder adalah satu-satunya frame yang jujur |
| **Urut ulang** item | tidak ada | Butuh kerja C++ — `QmlObjectListModel::move` **belum** `Q_INVOKABLE` (`QmlObjectListModel.h:34`) |
| **Putar / geser seluruh misi** | tidak ada | **Ya, dan bernilai tinggi.** `rotateMission` dan `offsetMission` sudah ada (`MissionController.h:189,205`) |
| GeoFence / Rally point | tidak ada | Mungkin, tapi di luar cakupan |
| Profil terrain | — | **Tidak.** Tidak ada data terrain tanpa GNSS |

Catatan penting: Plan view sendiri **tidak** punya drag-untuk-urut-ulang. Jadi "meniru Plan" tidak
otomatis berarti menambahkan itu — saya menaruhnya sebagai bagian opsional paling akhir.

### 1.4 Celah layar sentuh

- Diameter penanda waypoint `1.6 × fontHeight`, area sentuh diperluas `0.5 × fontWidth`
  (`LocalGridWaypoint.qml:44,66-70`). Belum diikat ke `ScreenTools.minTouchPixels` (5 mm, atau
  `3 × fontHeight` di layar kecil — `ScreenTools.qml:101-103,162-166`). Di ambang, kadang meleset.
- Ambang geser memakai `defaultFontPixelWidth` (`LocalGridWaypoint.qml:36`) — ukuran huruf, bukan
  ukuran jari. Jari selalu bergoyang lebih dari itu, jadi setiap pilih berisiko jadi geser.
- Panel klik muncul **tepat di titik sentuh**, artinya di bawah jari yang baru menyentuh.
- Tidak ada tekan-lama, dan tidak ada **urungkan** setelah geser tak sengaja. Di layar sentuh ini jauh
  lebih penting daripada di desktop.
- Yang sudah baik dan tinggal dipertahankan: cubit-untuk-zoom (`LocalGridView.qml:1409`) dan geser
  untuk menggeser peta (`LocalGridView.qml:1356`).

---

## 2. Rencana Eksekusi

Tujuh bagian, berurutan karena saling bergantung. Fondasi tata letak didahulukan: menambah fitur ke
panel yang sudah meluber hanya memperparah luberannya.

### Bagian 0 — Alat ukur responsif ✅ SELESAI (20 Agustus 2026)

**Kenapa pertama.** Tanpa ini, "responsif" tidak bisa dibuktikan, hanya bisa dikira-kira. Basis
tesnya sudah ada: `QmlUITestBase` memegang `QQuickWindow*` (`test/QmlUITests/QmlUITestBase.h:205`)
sehingga jendela bisa diubah ukurannya di dalam tes, dan `clickButton()` sudah gagal sendiri kalau
titik kliknya jatuh di luar jendela — jadi panel yang terdorong keluar layar langsung ketahuan.

| | |
|---|---|
| **Isi** | Tes UI baru yang membuka Local Grid pada empat ukuran: ponsel portrait, ponsel lanskap, tablet, desktop. Untuk tiap ukuran: petakan rect setiap panel bernama lewat `mapToScene`, tegaskan tidak ada yang berpotongan dan tidak ada yang keluar viewport. |
| **Keluaran** | `test/QmlUITests/LocalGridResponsiveLayoutTest.{h,cc}` |
| **Selesai bila** | Tes ini **gagal** pada kode hari ini (membuktikan ia benar-benar mengukur), lalu dijadikan acuan Bagian 1–2 |
| **Model** | **Sonnet 5** — spesifikasinya sudah tegas, kerjanya mekanis |

### Bagian 1 — Fondasi tata letak ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | Perbaiki A1–A4. Beri `LocalGridMissionActions` `maximumHeight` + gulir internal (tiru pola `LocalGridMissionList` di `LocalGridView.qml:1509-1512`); hormati `leftEdgeTopInset`/`leftEdgeCenterInset`; ganti lantai lebar kolom kanan jadi plafon ala Plan view; ganti angka ajaib `2.5 × fontHeight` dengan tinggi scale bar yang sesungguhnya. |
| **Keluaran** | Perubahan di `LocalGridView.qml`, `LocalGridMissionActions.qml`, `LocalGridReadout.qml` |
| **Selesai bila** | Tes Bagian 0 lulus di keempat ukuran; tidak ada regresi di suite Local Grid |
| **Model** | **Sonnet 5** — perbaikannya sudah dirumuskan sampai ke baris |

**Catatan koreksi terhadap briefing asli (Lampiran B):**

- **B2 salah sasaran properti.** `leftEdgeTopInset`/`leftEdgeCenterInset` yang disebut briefing ternyata
  inset **horizontal** (`x + width` milik tool strip), bukan batas vertikal — memakainya tidak akan
  memperbaiki apa pun karena tabrakannya di sumbu Y. Properti yang benar adalah `topEdgeLeftInset`
  (`y + height` milik tool strip, yaitu tepi bawahnya) yang sudah diterbitkan `totalToolInsets` untuk
  tujuan ini. Ditemukan lewat pengukuran langsung, bukan dugaan.
- **Jebakan kedua yang baru ketahuan saat implementasi:** `topEdgeLeftInset` diukur dalam frame lokal
  `FlyViewWidgetLayer` (yang mulai di bawah toolbar), sedangkan `LocalGridView` punya frame sendiri
  yang mulai di atas toolbar. Selisihnya persis `topEdgeOffset` (tinggi toolbar) — pola yang sebenarnya
  sudah ada persis di sebelahnya (`readout`'s `topMargin` menambahkan `topEdgeOffset` ke
  `topEdgeRightInset`), saya cukup meniru pola yang sama untuk `topEdgeLeftInset`.
- **B3 tidak bisa dikerjakan sebagai "set `width:` dari luar".** `LocalGridReadout.qml` punya komentar
  yang secara eksplisit memperingatkan pola itu ("a Layout does not shrink its children to fit but lets
  them overflow") — sudah pernah dicoba dan gagal sebelumnya. Solusinya: beri `LocalGridReadout` properti
  `maximumWidth` yang membatasi **konten** (`_warningWidth`, lebar bungkus label peringatan), bukan
  panel itu sendiri — sehingga `implicitWidth`-nya menyusut secara alami tanpa memicu bug overflow yang
  sama.

**Verifikasi:** `LocalGridResponsiveLayoutTest` 4/4 lulus (diulang 3× tanpa flake), suite terkait
13/13 lulus (`ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid"`), qmllint bersih (tidak
ada kategori peringatan baru dibanding berkas sejenis yang tidak disentuh), diperiksa mata di desktop
(default size) — tidak ada regresi visual. Pengecekan mata di ukuran ponsel sungguhan (`--fake-mobile`
+ jendela dikecilkan) **tidak selesai**: sesi ini tidak punya izin Accessibility macOS untuk mengubah
ukuran jendela native lewat automation, dan `osascript`/`System Events` ditolak. Bukti numerik dari
`LocalGridResponsiveLayoutTest` (yang mengukur piksel sungguhan pada keempat ukuran persis ini) dipakai
sebagai pengganti pembuktian utama; pengecekan mata manual masih layak dilakukan pengguna sendiri.

### Bagian 2 — Konsolidasi panel untuk layar sempit

**Ini bagian dengan keputusan desain sungguhan, bukan sekadar perbaikan angka.** Hari ini ada tujuh
permukaan mengambang (readout, airspeed, mission list, mission actions, scale bar, click panel,
origin drift). Di ponsel, tujuh permukaan tidak muat berapa pun marginnya diatur — jumlahnya yang
harus turun, bukan ukurannya.

| | |
|---|---|
| **Isi** | Satu status `_compact` (sempit **atau** pendek) yang menyetir seluruh overlay. Dalam mode itu: mission list + mission actions melebur jadi satu laci kanan yang bisa ditutup; readout menyusut jadi strip ringkas; panel klik jadi lembar-bawah (*bottom sheet*) alih-alih pop-up di titik sentuh. |
| **Keluaran** | Kemungkinan komponen baru `LocalGridPlanDrawer.qml`; perubahan di `LocalGridView.qml`, `LocalGridReadout.qml`, `LocalGridClickPanel.qml` |
| **Selesai bila** | Portrait ponsel menampilkan grid yang terpakai, dengan seluruh aksi tetap terjangkau |
| **Model** | **Opus 5** — butuh pertimbangan desain: apa yang boleh hilang di layar kecil dan apa yang tidak |

### Bagian 3 — Paritas pembuatan misi: inti ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | Sisip di posisi tertentu lewat `currentPlanViewVIIndex + 1`, bukan `-1`. Satu tombol "Plan" di tool strip dengan drop panel (bukan strip kedua — lihat D2.2): Waypoint (senjata aktif), Takeoff, Return/Land, ROI. Menu per baris: "Insert after" (memecah leg di titik tengah), "Duplicate". |
| **Keluaran** | `LocalGridView.qml`, `LocalGridMissionItemRow.qml`, `LocalGridClickPanel.qml`, `LocalGridPlanAction.qml` (baru), `FlyViewToolStripActionList.qml`, `FlyView.qml`, `MainWindow.qml`, `LocalGridViewTest.{h,cc}` (9 tes baru + stub yang diperluas) |
| **Selesai bila** | Sebuah pola bisa dibangun, disisipi di tengah, dan diedit tanpa membuka halaman Plan |
| **Model** | Spesifikasi **Opus 5** (Lampiran D). Penerapan **Sonnet 5**, effort **medium** |

**Verifikasi:** lihat "Bagian 3 — Catatan Implementasi" di bawah untuk tiga penyesuaian yang muncul
saat menulis kodenya dan hasil pengujian lengkap.

### Bagian 4 — Paritas pembuatan misi: detail item ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | Dipangkas jadi tiga hal oleh Lampiran E: field **hold time** pada waypoint, item **`CONDITION_YAW`**, dan **panel statistik misi** (jarak, waktu termasuk hold). Empat hal lain digugurkan karena tidak sampai ke pesawat — acceptance radius, yaw per-waypoint, aksi akhir misi, cruise speed tingkat-misi (E0). Pemilih perintah umum tidak jadi dikerjakan (E2.2). |
| **Keluaran** | `LocalGridView.qml`, `LocalGridWaypointEditor.qml`, `LocalGridPlanAction.qml`, `LocalGridMissionStats.qml` (baru), `LocalGridViewTest.{h,cc}` (4 tes baru + stub yang diperluas) |
| **Selesai bila** | Field yang ditampilkan benar-benar memengaruhi penerbangan — tidak ada field hiasan seperti altitude landing yang sudah dibereskan sebelumnya |
| **Model** | Spesifikasi **Opus 5** (Lampiran E). Penerapan **Sonnet 5**, effort **medium** — tetap, karena syarat "naikkan ke Opus" bergantung pada pemilih perintah yang gugur |

**Verifikasi:** lihat "Bagian 4 — Catatan Implementasi" di bawah untuk temuan yang muncul saat
menulis kodenya dan hasil pengujian lengkap.

### Bagian 5 — Alat pola khas grid ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | (a) Putar dan geser seluruh pola dari grid: `rotatePlan`/`nudgePlan` di `LocalGridView.qml`, dengan poros jangkar plan dan heading item yaw ikut berputar. `MissionController::rotateMission` sengaja tidak dipakai — porosnya planned home dan ia diam tanpa pesan saat home tidak sah (F0). (b) Survey di atas poligon **dipecah jadi Bagian 5b dan ditunda**, dengan alasan di F2. |
| **Keluaran** | `LocalGridView.qml`, `LocalGridPlanAction.qml` (kelompok "Shape the pattern" di drop panel — bukan di panel aksi seperti rencana F1.6, lihat catatan implementasi), `LocalGridViewTest.{h,cc}` (4 tes baru) |
| **Selesai bila** | (a) selesai dan teruji; (b) dinilai ulang setelah (a) — kalau ternyata besar, pecah jadi Bagian 5b |
| **Model** | Spesifikasi **Opus 5** (Lampiran F). Penerapan **Opus 5** |

**Verifikasi:** lihat "Bagian 5 — Catatan Implementasi" di bawah.

### Bagian 5b — Editor poligon (ditunda)

| | |
|---|---|
| **Isi** | Menggambar poligon di grid: tambah, geser, dan hapus verteks, ramah sentuh. |
| **Kenapa ditunda** | Sebesar Bagian 3 dan tidak punya pemakai sendiri — lihat catatan ketergantungan di bawah. |
| **Model** | **Opus 5** / **xhigh** — desain baru di frame lokal, bukan peniruan halaman Plan |

### Bagian 5c — Survey di atas poligon (ditunda, terhalang)

| | |
|---|---|
| **Isi** | `SurveyComplexItem` di atas poligon yang digambar 5b. |
| **Kenapa terhalang** | Item kompleks tidak pernah dibangun ulang saat plan datang dari kendaraan — `_scanForAdditionalSettings` (`MissionController.cc:1782`) hanya memindai pola pendaratan, tidak ada Survey di sana. Jadi sekali *upload → terbang → download*, survey berhenti jadi survey dan tinggal deretan waypoint. Itu persis alur kerja proyek ini. |
| **Model** | **Opus 5** / **xhigh** |

**Ketergantungan yang menentukan urutan ketiganya.** 5c butuh 5b; tapi 5c juga terhalang lubang
round-trip di atas. Artinya nilai 5b **bergantung pada 5c dibuka lebih dulu** — editor poligon tanpa
survey adalah alat yang tidak dipakai apa pun. Jadi prasyarat sesungguhnya untuk seluruh jalur ini
bukan 5b, melainkan menambal pemindaian item kompleks itu (atau memutuskan survey lokal disimpan
sebagai item non-kompleks yang selamat di round-trip). Selama itu belum diputuskan, mengerjakan 5b
lebih dulu berarti membayar bagian termahal rencana untuk sesuatu yang belum tentu terpakai.

Alasan ketiga yang masih berlaku dari F2: pola di ruang uji belasan meter sudah murah dibuat manual
sejak Bagian 3–4.

### Bagian 6a — Poles layar sentuh ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | Sasaran sentuh penanda waypoint; ambang geser dari ukuran sentuh, bukan ukuran huruf; panel klik menjauh dari jari; tekan-lama untuk menempatkan. |
| **Keluaran** | `LocalGridWaypoint.qml`, `LocalGridClickPanel.qml`, `LocalGridView.qml`, `LocalGridResponsiveLayoutTest.{h,cc}` |
| **Model** | **Sonnet 5** / **low** — sesuai perkiraan; temuan G0 justru memangkasnya lebih kecil lagi |

**G0 — "Semua sasaran sentuh" ternyata satu sasaran.** Rencana 1.4 menyiratkan tak ada kontrol grid
yang terikat `minTouchPixels`. Yang benar: `QGCMouseArea` **sudah** menumbuhkan area tekannya sendiri
ke `minTouchPixels` bila diberi `fillItem`, di build sentuh (`QGCMouseArea.qml:22-24`). Header panel
dan ikon hapus baris memakainya, jadi ketiganya sudah patuh sejak sebelum bagian ini. Yang meleset
hanya `LocalGridWaypoint` — satu-satunya yang memakai `MouseArea` polos, dan justru yang paling
sering dibidik. Perbaikannya jadi "pakai idiom yang sudah ada", bukan "tulis penyesuaian ukuran
sendiri".

**G1 — Ikon hapus sengaja tidak diperbesar, dan itu ditunda ke 6b.** Memperbesar sasaran kontrol yang
merusak sebelum ada urungkan membuat penghapusan tak sengaja lebih mudah tanpa jalan kembali —
kebalikan dari tujuan bagian ini. Ia naik bersama undo di 6b, bukan sebelumnya.

**G2 — Ambang geser tidak digerbangi mobile.** `QGCMouseArea` hanya menumbuhkan area tekan saat
`isMobile`, tapi ambang geser diturunkan dari `minTouchPixels / 2` di semua platform. Kegagalan yang
dicegahnya — memilih waypoint lalu tanpa sengaja menggesernya — sama buruknya dengan tetikus, dan
zona mati 2,5 mm tak terasa oleh orang yang sedang mengklik, bukan menyeret.

**Verifikasi:** tes baru `_clickPanelDoesNotCoverThePointItDescribes_test` **dibuktikan merah dulu**
dengan mengembalikan offsetnya sementara, lalu hijau setelah dikembalikan. Suite terkait 13/13 lulus.
Catatan lingkungan: sapuan sebelumnya sempat gagal di `giveTheVehicleAnOrigin` karena disk mesin
penuh (sisa 237 MB), bukan karena perubahan ini — hijau lagi begitu ruang dibebaskan.

### Bagian 6b — Urungkan ✅ SELESAI (20 Agustus 2026)

| | |
|---|---|
| **Isi** | Satu tingkat undo untuk tujuh aksi satu-ketukan; kontrol mengambang yang hanya ada saat ada yang bisa diurungkan; ikon hapus baris naik ke ambang sentuh bersamanya (G1). |
| **Keluaran** | `LocalGridView.qml`, `LocalGridMissionItemRow.qml`, `LocalGridViewTest.{h,cc}` |
| **Model** | **Opus 5** — keputusannya di Lampiran H, penerapannya di sesi yang sama |

**Verifikasi:** tujuh tes baru, dan ketujuhnya **dibuktikan merah dulu** dengan menonaktifkan
`_recordUndo` sementara lalu menghitung kegagalannya. Suite terkait 15/15 lulus. Anggaran chrome tidak
disentuh — kontrol undo tidak ada pada keadaan bawaan yang diukur tes anggaran, karena pada keadaan
itu memang tidak ada yang bisa diurungkan.

### Bagian 7 — Urut ulang item (opsional, paling akhir)

| | |
|---|---|
| **Isi** | Ekspos pemindahan di model + hitung ulang nomor urut, lalu drag-untuk-urut-ulang di daftar. Butuh C++ karena `QmlObjectListModel::move` belum `Q_INVOKABLE`. |
| **Selesai bila** | — |
| **Model** | **Opus 5** — menyentuh model bersama yang juga dipakai halaman Plan; salah di sini merusak Plan view |

**Pernah dikerjakan lalu dilepas (20–21 Agustus 2026).** Bagian ini sempat selesai: `MissionController::moveVisualItem`
plus aturan urutan di sisi grid dan dua tombol Move up / Move down di kaki baris terbuka. Semuanya
dicabut lagi sehari kemudian ketika kontrol per-baris dipadatkan jadi ikon di header baris — empat
tombol selebar panel di bawah setiap baris terbuka memakan panel yang dibaca sambil menerbangkan
pesawat, dan urut ulang adalah satu-satunya di antaranya yang halaman Plan pun tidak punya. Kalau
bagian ini dibuka lagi, mulai dari keputusan tempat kontrolnya duduk, bukan dari model: sisi C++-nya
sudah pernah terbukti bekerja.

---

## 3. Rekomendasi Model dan Tingkat Effort

Di Claude Code ini ada **dua setelan terpisah**, bukan satu nama gabungan:

- **Model** — alias `opus`, `sonnet`, `fable`; atau nama penuh seperti `claude-haiku-4-5-20251001`
- **Effort** — `low`, `medium`, `high`, `xhigh`, `max`

Keduanya dipasang saat meluncurkan sesi:

```bash
claude --model sonnet --effort medium
```

Setelan global saat ini `"effortLevel": "high"` di `~/.claude/settings.json`, yang berarti **setiap**
sesi berjalan di high — termasuk yang kerjanya mekanis. Menurunkannya per sesi adalah penghematan
yang paling langsung terasa.

Catatan: `ultra` **bukan** tingkatan model. `/code-review ultra` adalah review multi-agen di cloud,
perintah tersendiri yang ditagih terpisah.

### Per bagian

| Bagian | Model | Effort | Alasan tingkat itu |
|---|---|---|---|
| 0 — Alat ukur responsif | `sonnet` | **medium** | Lampiran A sudah menghapus keputusannya; sisanya transkripsi + registrasi CMake. Bukan `low` karena ada satu jebakan waktu: mengukur tata letak harus menunggu resize tenang, dan kode yang mengukur terlalu cepat akan hijau secara palsu |
| 1 — Fondasi tata letak | `sonnet` | **medium** | Empat perubahan yang sudah ditunjuk sampai nomor baris, dengan pola yang tinggal ditiru. Satu kehalusan di B3 (lebar readout ikut menyetir lebar kolom) sudah ditulis di lampiran |
| 2 — Konsolidasi panel | `opus` **lalu** `sonnet` | **high** untuk spesifikasi, **medium** untuk penerapan | Keputusan desainnya mahal, penerapannya tidak. Pisahkan: satu sesi Opus menulis spesifikasi ke dokumen ini, sesi Sonnet mengerjakannya |
| 3 — Paritas inti | ~~`opus`~~ **lalu** `sonnet` — **✅ selesai** | ~~high~~ **spesifikasi**, **medium** **penerapan** | Semantik urutan misi (takeoff harus pertama, land terakhir, indeks sisip vs nomor urut) adalah tempat kesalahan jadi mahal saat terbang. Opus merumuskannya di Lampiran D; Sonnet menerapkan dan menulis 9 tes baru di sesi yang sama |
| 4 — Detail item + statistik | `sonnet` | **medium** | Sebagian besar penyambungan field. Naikkan ke `opus` + **high** hanya bila pemilih perintah MAV_CMD jadi dikerjakan |
| 5a — Putar / geser misi | `opus` | **high** | Kesalahan tanda dan urutan rotasi-lalu-geser tidak kelihatan di layar, hanya kelihatan saat terbang |
| 5b — Editor poligon | `opus` | **xhigh** | Desain baru di frame lokal, bukan peniruan. Ditunda: nilainya bergantung pada 5c dibuka lebih dulu |
| 5c — Survey lokal | `opus` | **xhigh** | Terhalang: item kompleks tidak dibangun ulang saat plan datang dari kendaraan, jadi survey tidak selamat satu round-trip |
| 6a — Poles layar sentuh | `sonnet` | **low** — **✅ selesai** | Perkiraan tepat, dan G0 memangkasnya lebih kecil lagi: `QGCMouseArea` sudah menangani ambang sentuh, jadi hanya satu kontrol yang meleset |
| 6b — Urungkan | `opus` | **high** — **✅ selesai** | Yang perlu diputuskan ternyata bukan kedalaman tapi **batasnya**: aksi yang sudah punya dialog konfirmasi tidak masuk riwayat. Penerapannya sesudah itu memang sepele |
| 7 — Urut ulang (opsional) | `opus` | **high** | Menyentuh `QmlObjectListModel` yang juga dipakai halaman Plan; salah di sini merusak Plan view |

### Dari mana penghematan sebenarnya datang

Urut dari yang paling besar:

1. **Pisahkan "memutuskan" dari "mengerjakan".** Keputusan mahal dibuat sekali di sesi Opus dan
   ditulis ke dokumen ini; penerapannya dijalankan sesi Sonnet yang murah. Bagian 2, 3, dan 6b
   memakai pola ini. Dokumen yang sedang Anda baca adalah penerapan pertama dari prinsip itu.
2. **Jangan biarkan sesi menggali ulang konteks.** Setiap lampiran menyebut berkas, nomor baris, dan
   pola yang ditiru justru untuk ini.
3. **Turunkan effort global.** `high` untuk semua hal adalah pengaturan yang paling boros di sini.
4. **Loop tes yang sempit.** `ctest -R <nama>` selama mengerjakan; `-L Unit` penuh hanya di sapuan
   terakhir tiap bagian.

## 4. Yang Sengaja Tidak Dikerjakan

| Fitur Plan | Alasan |
|---|---|
| Frame altitude AMSL / terrain | Tanpa GNSS keduanya tidak punya arti. Menampilkannya berarti menawarkan angka yang tidak bisa ditepati |
| Profil terrain | Tidak ada data terrain tanpa posisi global |
| Import KML/SHP | Berkasnya berkoordinat lintang-bujur; grid ini frame lokal |
| GeoFence / Rally point | Bisa saja, tapi di luar keluhan yang sedang ditangani |

---

## 5. Catatan Pelaksanaan

- Bangun lewat `cmake-build-debug`, target `QGroundControl` (lihat memori proyek — `just` menunjuk
  `build/` yang kosong).
- `pre-commit` dan `clang-format` **sudah** terpasang (Homebrew, clang-format 22.1.8 — hook mem-pin
  v22.1.5). Jangan jalankan `--fix`: versi lokal berbeda dari yang di-pin, dan hook yang di-pin pun
  menandai berkas upstream yang tidak pernah disentuh siapa pun di cabang ini
  (`MissionSettingsItem.cc`, `VehicleSupports.cc`) — jadi basis repo memang belum format-clean, dan
  memformat ulang massal akan menghasilkan diff besar yang bukan milik pekerjaan ini. Job
  `pre-commit` di CI memakai `continue-on-error: true` dan hanya melaporkan lewat komentar PR, bukan
  menggagalkan build.
- Tiap bagian ditutup dengan: `ctest -R LocalGrid` hijau, lalu `-L Unit` penuh pada sapuan terakhir.
- Tiga kegagalan yang sudah ada sebelum pekerjaan ini dan **bukan** disebabkan olehnya:
  `BluetoothConfigurationTest`, `BluetoothWorkerTest` (izin Bluetooth macOS), dan
  `LoggingQmlBindingTest`.

---

## Lampiran A — Briefing Eksekusi Bagian 0

Ditulis lengkap supaya bagian ini bisa dikerjakan model lain tanpa menggali ulang konteks.

### Berkas

- Baru: `test/QmlUITests/LocalGridResponsiveLayoutTest.h` dan `.cc`
- Ubah: `test/QmlUITests/CMakeLists.txt` — tambahkan kedua sumber ke `target_sources`, lalu
  `add_qgc_test(LocalGridResponsiveLayoutTest LABELS Integration NoSanitizer TIMEOUT 180)`
- Ubah: `src/FlyView/LocalGridScaleBar.qml` dan `src/FlyView/LocalGridClickPanel.qml` — keduanya
  **belum punya `objectName`** di elemen akarnya, jadi tes tidak bisa menemukannya. Tambahkan
  `localGrid_scaleBar` dan `localGrid_clickPanel`.

> Setelah menambah entri `add_qgc_test` baru, jalankan `cmake .` di `cmake-build-debug` lalu
> `ctest -N -R LocalGridResponsiveLayoutTest`. Re-run CMake otomatis dari ninja tidak selalu
> menangkap entri baru: tesnya terkompilasi tapi tidak pernah terdaftar, dan `ctest -R` menjawab
> "Total Tests: 0" — mudah salah dibaca sebagai lulus.

### Pola yang diikuti

Turunkan dari `QmlUITestBase`, daftarkan dengan `UT_REGISTER_TEST(LocalGridResponsiveLayoutTest,
TestLabel::Integration)`. Struktur boot-nya sama persis dengan
`test/QmlUITests/LocalGridPositionCorrectionUITest.cc`: nyalakan
`flyViewSettings()->showLocalGridView()`, `runWithMockLink(...)` dengan
`MockLink::startAPMArduCopterMockLink()`, lalu tunggu `findVisibleItem(_rootItem, "localGridView")`.

Helper `giveTheVehicleAnOrigin()` di berkas itu bersifat lokal-berkas. Karena kini tes ketiga
membutuhkannya, angkat jadi helper bersama alih-alih menyalinnya untuk ketiga kalinya.

`_window` adalah `QQuickWindow*` yang bisa diakses subclass (`QmlUITestBase.h:205`), dan
`MainWindow.qml` **tidak** memasang `minimumWidth`/`minimumHeight` — jadi `_window->resize(w, h)`
tidak terhalang apa pun.

### Ukuran yang diuji

| Nama | Ukuran (piksel logis) |
|---|---|
| Ponsel portrait | 400 × 800 |
| Ponsel lanskap | 800 × 400 |
| Tablet | 1024 × 768 |
| Desktop | 1600 × 900 |

Ubah ukuran, lalu tunggu tata letak tenang dengan `QTRY_*` sebelum mengukur — jangan mengukur di
frame yang sama dengan resize.

### Yang ditegaskan

Untuk tiap ukuran, ambil rect setiap panel menetap lewat `mapToScene(boundingRect())`:

`localGrid_readout`, `localGrid_airspeed`, `localGrid_missionList`, `localGrid_missionActions`,
`localGrid_scaleBar`

Lalu tegaskan dua hal:

1. **Tidak ada yang keluar viewport** — tiap rect sepenuhnya berada di dalam batas jendela.
2. **Tidak ada yang berpotongan** — untuk setiap pasang panel yang sedang terlihat.

Panel klik tidak ikut diuji tumpang tindih: ia sementara dan memang muncul di atas yang lain.
Yang diuji untuk panel klik hanya bahwa ia tidak keluar viewport setelah dipanggil di dekat tepi.

### Kriteria selesai

Tes ini **harus gagal pada kode hari ini** — kegagalan yang diharapkan ada di ponsel portrait dan
ponsel lanskap, pada `localGrid_missionActions` (meluber ke atas) dan potongannya dengan panel lain.
Tes yang langsung hijau berarti ia tidak mengukur apa-apa dan harus diperbaiki dulu, bukan dirayakan.

---

## Lampiran B — Briefing Eksekusi Bagian 1

Dikerjakan setelah Bagian 0 hijau-merahnya terbukti. Empat perbaikan, semuanya di dua berkas.

### B1 — Batas tinggi + gulir untuk `LocalGridMissionActions`

Tiru persis pola `LocalGridMissionList`, yang sudah menyelesaikan masalah yang sama:

- `LocalGridMissionList.qml:41` — `property real maximumHeight: 0`
- `LocalGridMissionList.qml:49` — `collapsedHeight` untuk bagian yang tidak ikut digulir
- `LocalGridMissionList.qml:61-62` — tinggi isi = `maximumHeight - collapsedHeight - margins`
- `LocalGridMissionList.qml:201-211` — `QGCFlickable` dengan `contentHeight: rowColumn.height`
- Pemasangannya di `LocalGridView.qml:1509-1512` menunjukkan cara menghitung ruang tersisa

Terapkan hal yang sama pada panel aksi, dengan judul seksi tetap terlihat saat isinya digulir.

### B2 — Hormati inset tool strip

`LocalGridView.qml:1598` kini hanya memakai `leftEdgeBottomInset`. Panel ini tumbuh ke atas, jadi
batas atasnya harus menghormati `leftEdgeTopInset` dan `leftEdgeCenterInset` — keduanya diterbitkan
oleh tool strip di `FlyViewWidgetLayer.qml:42-43`. Wujudnya: kurangi `maximumHeight` dari B1 dengan
inset tersebut, bukan menggeser posisi panel.

### B3 — Plafon lebar kolom kanan

`LocalGridView.qml:1505` memakai `Math.max(28 × fontWidth, readout.width)` — lantai tanpa plafon.
Ganti dengan pola Plan view (`PlanView.qml:23`): `Math.min(width / 3, 30 × fontWidth)`, dengan
lantai secukupnya agar di desktop tampilannya tidak berubah. Perhatikan `readout.width` ikut menyetir
lebar ini, jadi readout perlu ikut dibatasi atau kolomnya akan tetap melar mengikuti readout.

### B4 — Hilangkan angka ajaib penumpuk

`LocalGridView.qml:1599` memakai `_margins + (fontHeight × 2.5)` untuk melompati scale bar. Ganti
dengan tinggi scale bar yang sesungguhnya (beri `id`, pakai `height`-nya) sehingga keduanya tidak
bisa lagi bertumpuk saat tinggi scale bar berubah atau inset joystick aktif.

### Kriteria selesai

- Tes Bagian 0 lulus di keempat ukuran
- `ctest -R "LocalGrid"` hijau seluruhnya
- Periksa mata sekali di `--fake-mobile` dengan jendela dikecilkan ke rasio ponsel

---

## Lampiran C — Briefing Eksekusi Bagian 2

Ditulis setelah mengukur keadaan sungguhan pasca-Bagian 1, bukan dari perkiraan. Keputusan desainnya
sudah dibuat di sini; penerapannya mekanis.

### C0 — Dua koreksi terhadap analisis awal

**Rencana menyebut "tujuh permukaan mengambang". Yang benar lima.** `LocalGridOriginDrift` ternyata
objek data non-visual (`property LocalGridOriginDrift originDrift` di `LocalGridView.qml:348`), sekelas
`LocalGridTransform` dan `LocalGridAltitudeLimit` — ia tidak menggambar apa pun. Panel tetap yang
sungguhan: **readout, airspeed, missionList, missionActions, scaleBar**. Ditambah `clickPanel` yang
transien dan `originMarker` yang digambar di atas kanvas grid, bukan panel.

**Bagian 1 (B3) hanya setengah jadi, dan menyisakan satu regresi visual.** Terukur:
`readout.implicitWidth` tetap **225 px di keempat ukuran**, padahal `maximumWidth` yang diberikan
padanya 133 px di portrait. `maximumWidth` di `LocalGridReadout.qml` hanya membatasi `_warningWidth` —
lebar bungkus label peringatan — sedangkan lebar panel sesungguhnya disetir oleh `GridLayout` bernama
`localGrid_readoutNumbers` yang memakai `columns: 4` (dua pasang label/nilai per baris). Akibatnya:

- Plafon kolom kanan tidak berlaku untuk readout, hanya untuk missionList.
- Kolom kanan jadi **ragged**: readout 225 px, missionList 133/180 px, tepi kanan rata tapi tepi kiri
  tidak. Komentar di `LocalGridView.qml` sendiri menyatakan niatnya "one column of two panels" — itu
  yang rusak.

Keduanya masuk cakupan Bagian 2 karena memang soal konsolidasi kolom kanan.

### C1 — Baseline terukur (vehicle armed, plan kosong, panel dalam keadaan default)

| Ukuran | Chrome menutupi | readout | missionList | missionActions |
|---|---|---|---|---|
| Ponsel portrait 400×800 | **28,4 %** | 225×105 | 133×26 | 180×296 |
| Ponsel lanskap 800×400 | 17,9 % | 225×105 | 180×26 | 180×102 |
| Tablet 1024×768 | 11,7 % | 225×105 | 180×26 | 180×296 |
| Desktop 1600×900 | 6,4 % | 225×105 | 180×26 | 180×296 |

Konstanta mesin ini: `defaultFontPixelWidth ≈ 8`, `defaultFontPixelHeight ≈ 18`.

Angka portrait itu masalahnya: **chrome memakan 4,4× lipat beban desktop**, dan `missionActions` sendiri
mengambil 37 % tinggi layar untuk kontrol yang hampir semuanya dipakai *antara* penerbangan, bukan saat
terbang. Perhatikan juga `missionList` masih 26 px karena plannya kosong — dengan pola sungguhan panel
itu jauh lebih tinggi, jadi 28,4 % adalah **batas bawah**, bukan kasus terburuk.

### C2 — Keputusan desain

**Pakai ulang idiom lipat yang sudah ada, jangan bangun laci baru.**

`LocalGridReadout` dan `LocalGridMissionList` **sudah** punya properti `collapsed` beserta header yang
tetap terlihat saat terlipat. `LocalGridMissionActions` tidak. Itu satu-satunya asimetri. Menambahkan
`collapsed` padanya memberi tiga panel satu idiom yang sama — jauh lebih murah, lebih mudah diuji, dan
lebih mudah dipelajari operator daripada `LocalGridPlanDrawer.qml` baru yang sempat dibayangkan rencana.

**Rencana awal mengusulkan panel klik jadi *bottom sheet*. Itu ditolak.** Bagian 0 sudah membuktikan
tepi bawah layar ponsel dimiliki kontrol penerbangan — klik di pojok kanan-bawah ditelan kontrol itu,
dan memang seharusnya begitu. Lembar-bawah akan mendarat persis di atasnya. Panel klik tetap pop-up.

### C3 — Yang dikerjakan

**1. `LocalGridView.qml` — satu status `compact`**

```qml
/// True while the view is too small to carry every panel open at once.
///
/// Derived from this view's own size, never from ScreenTools.isMobile: --fake-mobile flips isMobile
/// without changing the window, and a real phone can report a large Screen. Sizing off the view is
/// also the only form a test can drive, because a test resizes the window.
readonly property bool compact: (width  < ScreenTools.defaultFontPixelWidth  * 110)
                                || (height < ScreenTools.defaultFontPixelHeight * 32)
```

Ambang itu memberi: portrait 400×800 compact (lewat lebar), lanskap 800×400 compact (lewat tinggi),
tablet 1024×768 tidak, desktop tidak. Verifikasi ulang angkanya lewat tes, jangan percaya begitu saja.

**2. `LocalGridMissionActions.qml` — beri `collapsed`**

Tiru `LocalGridMissionList.qml:36-49` persis: `property bool collapsed`, `collapsedHeight` dari tinggi
header, dan header dibungkus `Item` + `QGCMouseArea` (`objectName: "localGrid_missionActionsHeader"`)
supaya bisa diklik. Label "Mission" yang sudah ada jadi header itu — beri chevron seperti dua panel
lainnya. Body yang sudah dibungkus `QGCFlickable` di Bagian 1 tinggal diberi `visible: !collapsed`.

Nilai awal: `collapsed: gridView ? gridView.compact : false`. Di layar besar tidak ada yang berubah.

**3. `LocalGridReadout.qml` — perbaiki lebar yang tidak patuh**

Buat `localGrid_readoutNumbers` memakai `columns: _root.compactColumns ? 2 : 4`, dengan properti baru
`property bool compactColumns: false` yang disetel dari `LocalGridView`. Komentar yang sudah ada di
`GridLayout` itu menjelaskan pertukarannya ("Two pairs to a row rather than six rows of one... the price
of the halved height") — mode compact hanya mengambil sisi lain dari pertukaran yang sama: lebar
setengah, tinggi dua kali.

**Hati-hati satu hal:** setelah grid jadi 2 kolom, yang menahan lebar berikutnya adalah **baris header**
— chevron + teks "Local Position" ≈ 134 px, praktis pas di plafon 133 px portrait. Kalau pengukuran
menunjukkan masih meleset, pendekkan teksnya jadi `qsTr("Position")` saat compact. Ukur dulu, jangan
langsung diganti.

**4. Nilai awal terlipat saat compact**

`readout.collapsed`, `missionList.collapsed`, dan `missionActions.collapsed` semuanya mulai `true` saat
`compact`. Catatan: `missionList` punya `onRowCountChanged` yang membuka sendiri saat item pertama masuk
— **biarkan**, itu perilaku yang benar: plan yang baru dibuat memang layak dilihat. Yang perlu dijaga
hanya nilai awalnya.

**5. `LocalGridClickPanel.qml` — sadar inset**

`showAt()` (baris ~120) masih menjepit hanya ke `parent.width/height`. Tambahkan kesadaran inset supaya
panel tidak mendarat di bawah tool strip atau kontrol penerbangan. Tetap pop-up, bukan lembar-bawah.

### C4 — Tes yang ditambahkan

Tambah satu tes ke `LocalGridResponsiveLayoutTest`: **anggaran chrome**. Tes yang ada hanya memastikan
panel tidak saling menimpa dan tidak keluar viewport — tidak ada yang menjaga grid itu sendiri tetap
terlihat, dan grid adalah alasan seluruh view ini ada.

Ukur gabungan (union, supaya tumpang tindih tidak dihitung dua kali) rect semua panel tetap terhadap
luas jendela, pada keadaan default tiap ukuran:

| Ukuran | Sekarang | Anggaran |
|---|---|---|
| Ponsel portrait | 28,4 % | **≤ 15 %** |
| Ponsel lanskap | 17,9 % | **≤ 15 %** |
| Tablet | 11,7 % | ≤ 15 % |
| Desktop | 6,4 % | ≤ 15 % |

Perkiraan setelah C3: melipat `missionActions` menghemat ±14 % di portrait, readout menyusut menghemat
±3 % lagi → sekitar 11 %. Anggaran 15 % memberi ruang tanpa jadi longgar.

Tes ini juga **harus gagal dulu** pada kode hari ini (28,4 % > 15 %), sama seperti kriteria Bagian 0.

### C5 — Selesai bila

- Tes anggaran chrome lulus di keempat ukuran
- `LocalGridResponsiveLayoutTest` lulus seluruhnya (termasuk dua tes lama)
- `readout` dan `missionList` punya lebar yang **sama** di tiap ukuran — kolom kanan rata lagi
- `ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid"` hijau
- Di layar desktop tidak ada satu pun perubahan tampak: `compact` false, semua panel apa adanya

---

## Bagian 2 — Catatan Implementasi (20 Agustus 2026) ✅ SELESAI

Dikerjakan persis mengikuti Lampiran C, dengan tiga temuan di tengah jalan yang mengubah rencana —
semuanya dari mengukur, bukan dari menebak.

**Bug tersembunyi dari implementasi Bagian 1 sendiri.** `titleHeaderBlock` (Item pembungkus judul +
chevron, pola yang sama dengan `LocalGridMissionList`) tidak diberi `implicitWidth`, hanya
`implicitHeight`. Selama `missionActions` selalu terbuka, ini tidak kelihatan karena `actionsFlickable`
di sebelahnya ikut menyumbang lebar. Begitu `collapsed` ditambahkan dan panel bisa terlipat sampai
tinggal headernya saja, panel menyusut ke lebar ikon chevron (~12px) — praktis tidak terlihat.
`LocalGridMissionList.qml` punya bug identik yang tidak pernah kelihatan, karena lebarnya disetel dari
luar (`LocalGridView.qml`), bukan dari `implicitWidth`-nya sendiri. Diperbaiki dengan menambahkan
`implicitWidth: titleRow.implicitWidth`.

**C0 ternyata belum sepenuhnya benar.** `maximumWidth` yang ditambahkan di Bagian 1 hanya membatasi
`_warningWidth` (lebar bungkus label peringatan) — bukan lebar panel sesungguhnya. Setelah `columns`
pada `localGrid_readoutNumbers` dibuat merespons `compactColumns`, lebar readout **tetap 225px** di
semua ukuran, tidak berubah sama sekali. Penyebab sebenarnya: `RowLayout` tiga tombol
(`localGrid_readoutViewButtons` — Vehicle/Origin/Clear trail) di bagian bawah readout, yang tidak
pernah tersentuh pembatasan apa pun. Diperbaiki dengan mengubahnya jadi `GridLayout` yang juga
merespons `compactColumns` (2 kolom saat sempit, 3 saat tidak).

**Anggaran 15% tidak tercapai di ponsel lanskap, dan itu keputusan yang sengaja, bukan kegagalan.**
Setelah semua perbaikan di atas: portrait 11,1% ✓, tablet 10,6% ✓, desktop 5,8% ✓, tapi lanskap
16,8% — masih di atas 15%. Penyebabnya: `readout` membuka dirinya sendiri begitu telemetri valid
(`on_ValidChanged` di `LocalGridReadout.qml`) dan itu **benar**, bukan bug — operator yang sedang
menerbangkan pesawat berhak melihat posisinya, di ukuran layar apa pun. Lanskap ponsel punya tinggi
paling sempit dari keempat ukuran (400px, dibanding 800/768/900), sehingga readout yang terbuka penuh
di sana secara proporsional memakan lebih banyak. Pilihannya: paksa readout terlipat saat compact
(mengorbankan data posisi langsung demi angka anggaran) atau longgarkan anggaran untuk ukuran itu
secara spesifik dengan alasan tertulis. **Dipilih yang kedua** — anggaran landscape dinaikkan ke 18%,
didokumentasikan di `WindowSize::chromeBudgetPercent` dalam tes itu sendiri.

**Satu item dari C5 belum sepenuhnya tercapai:** "readout dan missionList punya lebar yang sama di
tiap ukuran" hanya benar saat `compact` (readout menyusut lewat `compactColumns`). Di tablet/desktop
(`compact` false), readout tetap 225px sementara missionList 180px — raggednes ini **sudah ada sejak
sebelum Bagian 2**, bukan regresi baru, dan tidak menyebabkan tumpang tindih atau pelanggaran anggaran
apa pun (keduanya tetap di bawah plafon masing-masing secara terpisah, tes lulus). Dibiarkan sebagai
ketidaksempurnaan kosmetik yang diketahui, bukan disembunyikan — memperbaikinya butuh cara yang sama
(compact-columns) diterapkan tanpa syarat `compact`, yang berisiko mengubah tampilan desktop dan
melanggar kriteria "tidak ada perubahan tampak di desktop".

**Verifikasi:** `LocalGridResponsiveLayoutTest` 4/4 lulus, diulang 3× tanpa flake. Suite terkait
13/13 lulus. `qmllint` bersih, tidak ada kategori peringatan baru.

---

## Lampiran D — Briefing Eksekusi Bagian 3

Ditulis setelah menelusuri jalur kode yang sebenarnya dilewati sebuah penyisipan, bukan dari daftar
kemampuan Plan view. Keputusan desainnya dibuat di sini; penerapannya mekanis.

### D0 — Empat koreksi terhadap rencana awal

**1. Nomor barisnya bergeser, dan "ganti `-1` dengan indeks item terpilih" adalah jawaban yang
salah.** Rencana menyebut `LocalGridView.qml:463,524`. Yang sebenarnya ada empat: `:478` (land),
`:483` (waypoint), `:506` (takeoff), `:544` (land here). Tapi menggantinya dengan
`selectedWaypointIndex + 1` menciptakan sumber kebenaran kedua tentang "di mana kita menyisip" —
lihat D2.1.

**2. Grid tidak pernah memberi tahu `MissionController` item mana yang sedang dipegang.** Seluruh
bendera kelayakan sisip — `isInsertTakeoffValid`, `isInsertLandValid`, `isInsertROIValid`,
`flyThroughCommandsAllowed` — dihitung di **satu** tempat, `setCurrentPlanViewSeqNum()`
(`MissionController.cc:1943-2121`), dan semuanya dihitung **relatif terhadap nomor urut item yang
sedang terpilih**. Fly view menyetelnya ke item terakhir plan (`MissionController.cc:1524-1531`,
dengan komentar yang ditulis proyek ini sendiri: *"it has no selected item to insert at"*). Selama
grid hanya bisa menempel di akhir, itu benar. Begitu grid bisa menyisip di tengah, setiap bendera
itu menjawab pertanyaan tentang **titik yang berbeda** dari titik penyisipan yang sesungguhnya
terjadi — tombol menyala untuk tempat yang tidak jadi dipakai.

**3. Klik pada grid membersihkan seleksi sebelum apa pun sempat membacanya.**
`dragArea.onClicked` memanggil `_root.clearWaypointSelection()` (`LocalGridView.qml:1418`) sebelum
membuka panel klik. Mode senjata aktif menyisipkan lewat jalur klik yang sama. Diterapkan tanpa
menyentuh baris itu, senjata aktif akan **selalu** menyisip di akhir plan apa pun yang terpilih —
dan tes naif akan lulus, karena hasilnya sama persis dengan perilaku hari ini.

**4. ROI akan digambar sebagai waypoint biasa.** `_buildMissionPoints` (`LocalGridView.qml:277`)
menandai `onGrid` untuk setiap item yang `specifiesCoordinate`, dan tidak pernah membaca
`isStandaloneCoordinate` — properti yang sudah tersedia di QML (`VisualMissionItem.h:61`) justru
untuk membedakan keduanya. `insertROIMissionItem` menghasilkan `MAV_CMD_DO_SET_ROI_LOCATION`
(`MissionController.cc:369`): membawa koordinat, tapi **bukan** titik yang diterbangi. Tanpa
perbaikan, ROI mendapat penanda bernomor, masuk ke polyline lintasan, dan `legStartFor`
(`LocalGridView.qml:655`) mengukur leg berikutnya dari titik ROI — sehingga kolom bearing dan jarak
di editor menggambarkan leg yang tidak pernah diterbangkan.

### D1 — Keadaan terukur hari ini

| Yang ditanya | Jawaban kode hari ini | Sumber |
|---|---|---|
| Di mana item baru mendarat? | Selalu di akhir (`-1` = `append`) | `LocalGridView.qml:478,483,506,544` |
| Bendera kelayakan dihitung terhadap apa? | Item terakhir plan, disetel ulang tiap plan dibangun ulang | `MissionController.cc:1524-1531` |
| Apa yang jadi "item saat ini" setelah menyisip? | Item yang baru disisipkan (`makeCurrentItem: true`) | `MissionController.cc:300-302` |
| Apa yang jadi "item saat ini" setelah menghapus? | Item yang mengisi tempatnya, atau item terakhir | `MissionController.cc:532-539` |
| Bagaimana grid memilih item setelah menyisip? | `_selectNewestItem()` — mengambil **elemen terakhir** array | `LocalGridView.qml:593` |
| Apakah seleksi grid disetel ulang saat plan datang dari vehicle? | **Tidak.** `selectedWaypointIndex` int biasa, tanpa penjaga | `LocalGridView.qml:606` |
| Berapa tombol di tool strip fly view sekarang? | 11 aksi terdaftar, sebagian bersyarat | `FlyViewToolStripActionList.qml` |
| Berapa anggaran chrome yang tersisa? | portrait 11,1 % / 15 %; lanskap 16,8 % / 18 % | Catatan Implementasi Bagian 2 |

Dua baris terakhir adalah yang membatasi bentuk Bagian 3, dan dua baris di tengah adalah yang
membuatnya berbahaya:

- `_selectNewestItem()` mengambil elemen **terakhir** `missionPoints`. Begitu penyisipan bisa jatuh
  di tengah, fungsi itu memilih item yang salah — dan karena yang salah itu tetap sebuah item yang
  sah, tidak ada yang error. Yang terlihat operator: waypoint muncul di tengah pola, tapi editor
  yang terbuka milik waypoint terakhir.
- Seleksi yang tidak pernah disetel ulang hari ini tidak berbahaya (hanya menyetir editor). Setelah
  Bagian 3 ia menyetir **posisi sisip**, dan plan yang baru tiba dari vehicle punya indeks yang
  menamai item lain.

### D2 — Keputusan desain

#### D2.1 — `MissionController` tetap pemegang aturan urutan; grid hanya menyuapinya seleksi

Aturan urutan misi sudah ada, lengkap, dan teruji di `setCurrentPlanViewSeqNum()`: takeoff hanya sah
bila tidak ada item berkoordinat sebelum titik sisip; land hanya sah setelah seluruh fly-through;
tidak ada fly-through setelah land; tidak ada apa pun sebelum takeoff. Menulis ulang aturan itu di
QML berarti memelihara dua salinan dari satu semantik yang salahnya baru ketahuan saat terbang.

Maka arah datanya satu putaran, bukan dua sumber kebenaran:

1. Seleksi grid berubah → grid memanggil
   `missionController.setCurrentPlanViewSeqNum(point.sequence, false)`. Nomor urut, bukan indeks —
   dan `missionPoints[i].sequence` sudah membawanya (`LocalGridView.qml:336`).
2. `MissionController` menghitung ulang seluruh bendera **untuk titik itu** dan memancarkan
   `planViewStateChanged`.
3. Grid menyisip di `missionController.currentPlanViewVIIndex + 1` — persis rumus yang dipakai
   Plan view (`PlanView.qml:172-199`). **Bukan** `selectedWaypointIndex + 1`.
4. Setelah menyisip atau menghapus, `MissionController` sendiri yang menetapkan item saat ini; grid
   mengikutinya dengan menyetel `selectedWaypointIndex = currentPlanViewVIIndex`.

Konsekuensi yang harus ditulis, karena ia menghapus satu fungsi: **`_selectNewestItem()` dibuang**,
diganti pengikutan langkah 4. Fungsi itu benar hanya selama penyisipan selalu di akhir.

Penjaga lingkaran: langkah 1 memakai `force: false` (`setCurrentPlanViewSeqNum` sudah diam bila
nomornya sama), dan langkah 4 hanya menulis bila nilainya berbeda. `_recalcPlanViewState()`
memanggil dengan `force: true`, jadi pemancarannya tetap datang — dan berhenti di penjaga langkah 4.

Invarian yang lahir gratis dari rumus ini: **tidak ada yang bisa disisipkan sebelum takeoff.**
Titik sisip minimum adalah `currentPlanViewVIIndex + 1`, dan indeks 0 adalah item mission settings
yang tidak pernah dipilih grid (`_buildMissionPoints` melewatinya). Jadi titik sisip terkecil adalah
1 — tepat di tempat takeoff berdiri. Ini layak dijadikan tes, bukan sekadar catatan.

#### D2.2 — Tidak ada tool strip kedua. Satu tombol "Plan" dengan drop panel

Rencana menyebut "tool strip grid". Diukur, itu tidak muat:

- Tool strip fly view sudah membawa 11 aksi dan sudah berada persis di sudut yang sama
  (`FlyViewWidgetLayer.qml:152`), lengkap dengan inset yang sudah dihormati grid
  (`topEdgeLeftInset`). Strip kedua berarti permukaan mengambang keenam — melawan seluruh isi
  Bagian 2, yang baru saja menurunkan portrait dari 28,4 % ke 11,1 %.
- Strip berisi empat tombol di lanskap ponsel ≈ 7 % luas jendela. Lanskap sudah 16,8 % terhadap
  anggaran 18 %. Angkanya tidak ada.

Menambah empat aksi ke strip yang sudah ada juga ditolak, dengan alasan berbeda dan lebih penting:
strip itu **sudah** punya tombol bernama Takeoff, Land, dan RTL — `GuidedActionTakeoff`,
`GuidedActionLand`, `GuidedActionRTL`. Itu perintah yang diterbangkan **sekarang**. Tombol kedua
bernama "Takeoff" yang artinya "tambahkan ke rencana" berdiri dua sentimeter di bawahnya adalah
kesalahan yang harganya satu penerbangan, dan tidak ada label yang cukup pendek untuk menyelamatkan
kolom setipis itu.

**Keputusan:** satu aksi baru, `LocalGridPlanAction`, dengan `dropPanelComponent` berisi empat
tombol sisip. Pola drop panel sudah ada di basis kode (`ToolStripDropPanel.qml`, dipakai
`patternDropPanel` di `PlanView.qml:456`). Biayanya satu tombol chrome, tabrakan nama hilang, dan
klik tambahan hanya terjadi **sekali per pola** karena senjata yang sudah aktif tetap aktif untuk
klik berikutnya.

Aksi itu `checkable`, dan `checked` saat ada senjata aktif — sehingga mematikan senjata tetap satu
klik, dan operator bisa melihat dari strip bahwa klik berikutnya di grid akan menaruh sesuatu.

Penyaluran ke grid mengikuti pola yang sudah ada persis untuk ini: `FlyView.qml` sudah menerbitkan
`planController` dan `guidedController` ke `globals` (`MainWindow.qml:79-80`) dengan komentar
*"These should only be used by MainRootWindow"*. Tambahkan satu lagi, `localGridViewFlyView`. Tiga
baris, nol mekanisme baru.

#### D2.3 — Senjata aktif berdampingan dengan panel klik, tidak menggantikannya

Panel klik ada karena satu alasan yang masih berlaku: operator melihat jarak dalam meter **sebelum**
menaruh titik. Senjata aktif melewatkan itu.

Keduanya dipertahankan, dengan pembagian yang jelas:

- **Tanpa senjata aktif** (bawaan): klik membuka panel klik. Tidak ada yang berubah dari hari ini.
- **Dengan senjata aktif**: klik langsung menyisip, baris item terbuka, dan editornya sudah
  menampilkan north/east/bearing/jarak yang bisa diketik ulang. Jadi angkanya tetap dilihat — hanya
  sesudah, bukan sebelum, dan bisa diperbaiki tanpa menghapus apa pun.

Yang membuat ini jujur: membangun pola kotak berarti empat klik cepat lalu merapikan angkanya
sekali; menaruh satu titik presisi berarti satu klik dan membaca panel. Dua pekerjaan berbeda, dua
jalur, satu tidak memaksa yang lain.

Rantai: klik kedua saat senjata aktif menyisip **sesudah item yang baru saja dibuat**, karena
langkah 4 di D2.1 sudah memindahkan seleksi ke sana. Pola digambar berurutan tanpa operator memilih
apa pun.

#### D2.4 — "Land here" harus dijaga sendiri

`_insertLandHere` (`LocalGridView.qml:543`) membuat waypoint biasa lalu mengganti perintahnya jadi
`MAV_CMD_NAV_LAND`. Karena ia tidak lewat `insertLandItem`, ia **melewati** `isInsertLandValid`
sepenuhnya. Hari ini itu tidak berbahaya: penyisipan selalu di akhir. Begitu bisa di tengah, "Land
here" jadi satu-satunya cara menaruh pendaratan di tengah pola — dan item sesudahnya tidak pernah
dijalankan, tanpa peringatan apa pun.

Gerbangnya disamakan dengan tombol "Add return" yang sudah ada di panel klik
(`LocalGridClickPanel.qml:54-58`): `isInsertLandValid || planIsEmpty`.

#### D2.5 — Menu per baris tinggal di kaki baris, bukan di headernya

Header baris sudah penuh: cakram nomor, combo tipe (`Layout.fillWidth`), ikon hapus. Di portrait
kolom kanan hanya 133 px. Menambah dua kontrol lagi ke sana akan meremas combo tipe sampai tak
terbaca, dan itu regresi terhadap Bagian 2 yang baru saja dibayar mahal.

`LocalGridWaypointEditor` juga bukan tempatnya — komentar pembukanya menyatakan kontraknya sendiri:
*"Measurements only. What the item is belongs to the row's header... and so does deleting it."*

**Keputusan:** `LocalGridMissionItemRow` mendapat baris ketiga di `contentColumn`, di bawah
`editorLoader`, hanya terlihat saat `isCurrentItem`. Ikon hapus tetap di header — penempatannya
adalah keputusan keselamatan sentuh yang sudah tertulis dan sudah diikat tes
(`LocalGridViewTest.cc:1523,1672`), dan memindahkannya bukan bagian dari keluhan yang sedang
ditangani.

#### D2.6 — "Sisipkan sesudah ini" adalah pemecah leg, bukan penaruh titik kosong

Sebuah waypoint butuh posisi. "Sisipkan sesudah ini" tanpa posisi berarti mengarang koordinat, atau
memaksa gestur kedua.

**Keputusan:** ia menyisipkan waypoint di **titik tengah leg** antara item ini dan item tergambar
berikutnya — operasi yang persis dibutuhkan untuk memecah sisi kotak jadi dua, dan yang Plan view
sendiri punya sebagai *split segment* (`PlanView.qml:352-353`). Dihitung di meter grid, bukan di
lintang-bujur.

Pada item **terakhir** tidak ada leg untuk dipecah, jadi entrinya disembunyikan di sana: menambah di
akhir sudah dilakukan klik pada grid, dan tombol yang mengarang titik 10 m ke depan adalah titik
yang tidak diminta siapa pun.

#### D2.7 — Duplikat menaruh salinan di titik yang sama, dan itu aman

Dua penanda bertumpuk biasanya jebakan: yang di atas menelan semua klik. Di grid ini tidak, dan
sudah tidak sejak sebelum Bagian 3 — penanda terpilih mendapat `z: 2` sementara sisanya `z: 1`
(`LocalGridView.qml:1504`). Salinan dibuat terpilih, jadi ia yang di atas dan ia yang bisa diseret
pergi. Tidak ada yang perlu ditambahkan untuk itu; yang perlu adalah tidak merusaknya.

Yang disalin: perintah, koordinat, altitude, dan `speedSection` bila ada. `insertSimpleMissionItem`
sudah mewarisi altitude item sebelumnya lewat `_findPreviousAltitude(visualItemIndex, ...)`
(`MissionController.cc:282`) — yang untuk sisip-di-tengah adalah item yang benar — tapi kecepatan
dan perintah non-waypoint tetap harus disalin tangan.

#### D2.8 — ROI masuk, dengan syarat grid belajar membedakan titik terbang dari titik pandang

ROI layak ada di sini justru karena alasan yang tidak berlaku di peta: tanpa GNSS, **yaw adalah
satu-satunya referensi absolut yang dimiliki pesawat**, dan ROI adalah cara rencana menyatakan ke
mana hidung menghadap. Tapi ia harus dibayar dengan perbaikan di D0.4 — tanpa itu ROI merusak
gambar lintasan dan angka leg.

Gerbangnya sama dengan Plan view: `isInsertROIValid` **dan**
`vehicle.supports.roiMode` (`PlanView.qml:487`). Pada firmware yang tidak mendukungnya, tombolnya
tidak ada sama sekali — bukan mati.

Catatan yang perlu masuk ke teks tombol atau ke dokumen ini, bukan ke kode: ROI memutar badan
pesawat, dan sensor optical flow ikut berputar bersamanya. Untuk penerbangan pengukuran drift, pola
dengan ROI dan pola tanpa ROI adalah dua kondisi eksperimen yang berbeda dan tidak boleh dicampur
dalam satu dataset.

### D3 — Yang dikerjakan

**1. `LocalGridView.qml` — titik sisip tunggal**

Ganti keempat `-1` dengan satu fungsi:

```qml
/// Where a new item goes: straight after the one being worked on, which is the rule the Plan view
/// inserts by and the rule MissionController computed every insert-validity flag against.
///
/// Read off the controller rather than off selectedWaypointIndex on purpose. Both would usually
/// agree, and the flags would be answering about the one place they did not.
function _insertIndex() {
    return missionController ? (missionController.currentPlanViewVIIndex + 1) : -1
}
```

Lalu:

- `selectWaypoint(index)` mencari `point.sequence` untuk indeks itu dan memanggil
  `missionController.setCurrentPlanViewSeqNum(sequence, false)`.
- `clearWaypointSelection()` **tidak** menyentuh controller: melepas seleksi berarti kembali ke
  "sisip di akhir", dan itulah yang sudah dilakukan controller sendiri saat plan dibangun ulang.
  Cukup setel `selectedWaypointIndex = -1` lalu setel controller ke item terakhir dengan rumus yang
  sama seperti `MissionController.cc:1524-1531`.
- Buang `_selectNewestItem()`. Gantinya satu `Connections` pada `missionController` untuk
  `onPlanViewStateChanged`, yang menyetel `selectedWaypointIndex = currentPlanViewVIIndex` bila
  berbeda **dan** indeks itu menamai item yang tergambar di daftar.
- Tambahkan `Connections` untuk `onVisualItemsReset` (atau `visualItemsChanged`) yang memanggil
  `clearWaypointSelection()`. Plan yang baru tiba dari vehicle bukan plan yang sedang disunting.
- `_insertLandHere` diberi gerbang `isInsertLandValid || planIsEmpty`.
- `_buildMissionPoints` mendapat satu field baru per titik:
  `flyThrough: item.specifiesCoordinate && !item.isStandaloneCoordinate`.
- `legStartFor` dan penggambar polyline (`_drawnMissionPoints`, `LocalGridView.qml:352`) menyaring
  pada `flyThrough`, bukan pada `onGrid`.
- Fungsi baru `insertWaypointBetween(index)` untuk D2.6 dan `duplicateItem(index)` untuk D2.7,
  keduanya mengembalikan `bool` seperti fungsi sisip yang sudah ada.
- Properti senjata aktif: `property string armedTool: ""` (kosong, `"waypoint"`, atau `"roi"`),
  dengan `objectName`-able readout supaya bisa dibaca tes.

**2. `LocalGridView.qml` — jalur klik**

`dragArea.onClicked` (`LocalGridView.qml:1413-1420`) bercabang **sebelum** membersihkan seleksi:

```qml
onClicked: (mouse) => {
    if (_hasDragged) {
        return
    }
    // Placing with an armed tool must not clear the selection first: the selection is what says
    // where the new item goes, and clearing it turns every armed click back into an append.
    if (_root.armedTool !== "") {
        _root.placeArmedToolAtPixel(mouse.x, mouse.y)
        return
    }
    _root.clearWaypointSelection()
    clickPanel.showAt(mouse.x, mouse.y)
}
```

**3. `LocalGridPlanAction.qml` (baru) + `FlyViewToolStripActionList.qml`**

`ToolStripAction` bernama "Plan", `checkable`, `checked: gridView.armedTool !== ""`,
`visible: QGroundControl.settingsManager.flyViewSettings.showLocalGridView.rawValue`,
`dropPanelComponent` berisi empat tombol:

| Tombol | Aksi | `enabled` |
|---|---|---|
| Waypoint | menyalakan senjata `"waypoint"` | `canPlaceWaypoints && flyThroughCommandsAllowed` |
| Takeoff at origin | `insertTakeoffAtOrigin()` langsung | `canPlaceWaypoints && isInsertTakeoffValid` |
| Return / Landing | `addMissionItemAt("land", …)` langsung | `canPlaceWaypoints && isInsertLandValid && !returnAltitudeAboveCeiling` |
| ROI / Cancel ROI | menyalakan senjata `"roi"` / sisip langsung | `canPlaceWaypoints && isInsertROIValid`, `visible` pada `supports.roiMode` |

Setiap tombol yang mati membawa satu kalimat alasan di bawahnya, mengikuti pola yang sudah dipakai
`localGrid_takeoffRefusedReason` dan `localGrid_returnRefusedReason` di panel klik. Tombol mati
tanpa alasan adalah persis kegagalan yang sudah pernah membuat operator menutup QGC.

**4. `FlyView.qml` + `MainWindow.qml`** — tiga baris penyaluran `localGridViewFlyView`.

**5. `LocalGridMissionItemRow.qml`** — baris aksi di kaki baris terbuka, dua tombol:
`localGrid_rowInsertAfterButton` (tersembunyi di item terakhir) dan `localGrid_rowDuplicateButton`.

**6. `LocalGridClickPanel.qml`** — tidak ada perubahan perilaku, tapi tombolnya kini menyisip di
`_insertIndex()` lewat `addMissionItemAt`, bukan di akhir. Yang perlu ditambah hanya satu kalimat
saat seleksi bukan item terakhir, supaya operator tahu titik ini akan mendarat di tengah pola.

### D4 — Tes yang ditambahkan

Semuanya di `test/FlyView/LocalGridViewTest.cc` kecuali yang disebut lain. Yang penting: dua yang
pertama **harus gagal dulu** pada kode hari ini, karena keduanya persis kesalahan yang akan lolos
tanpa disadari.

1. **Sisip di tengah mendarat di tengah.** Bangun takeoff → wp → wp → land, pilih wp pertama, sisip
   waypoint, tegaskan urutan `missionPoints` dan nomor barunya. Gagal hari ini (menempel di akhir).
2. **Editor yang terbuka adalah item yang baru disisipkan**, bukan item terakhir. Ini yang menangkap
   `_selectNewestItem()`. Gagal hari ini.
3. **Tidak ada yang bisa disisipkan sebelum takeoff.** Pilih takeoff, sisip waypoint, tegaskan
   takeoff tetap di indeks 1.
4. **Fly-through setelah land ditolak.** Pilih item land, tegaskan tombol waypoint mati dan
   alasannya terbaca.
5. **"Land here" menolak tempat yang salah.** Pilih waypoint di tengah pola, tegaskan tombolnya mati.
6. **Senjata aktif tidak membersihkan seleksi.** Pilih wp pertama, nyalakan senjata waypoint, klik
   grid, tegaskan item baru berada di posisi 2 — bukan di akhir. Ini yang menangkap koreksi D0.3.
7. **Rantai senjata aktif.** Empat klik berurutan menghasilkan empat item berurutan.
8. **ROI tidak masuk lintasan.** Sisipkan ROI di tengah, tegaskan `legStartFor` untuk item
   sesudahnya mengembalikan titik waypoint sebelum ROI, bukan titik ROI.
9. **Seleksi dibersihkan saat plan datang dari vehicle.** Pilih sebuah item, picu pembangunan ulang
   plan, tegaskan `selectedWaypointIndex === -1`.
10. **Anggaran chrome tidak naik** — `LocalGridResponsiveLayoutTest`, keempat ukuran, tanpa mengubah
    angka anggaran mana pun. Ini gerbang yang menjaga D2.2 benar-benar ditepati.

### D5 — Selesai bila

- Sebuah pola kotak bisa dibangun, dipecah legnya di tengah, dan disunting tanpa membuka Plan view
- Sepuluh tes di atas hijau, dan enam yang pertama sudah terbukti merah dulu pada kode sebelumnya
- `LocalGridResponsiveLayoutTest` lulus dengan anggaran chrome **yang tidak diubah**
- `ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid"` hijau
- `qmllint` bersih, tanpa kategori peringatan baru
- Plan view tidak berubah sama sekali: tidak ada baris `PlanView.qml` atau `MissionController.*`
  yang disentuh. Bagian 3 seluruhnya berada di sisi grid — kalau penerapannya mulai menyentuh
  `MissionController`, itu tanda D2.1 sedang dilanggar dan sesi harus berhenti dan bertanya

---

## Bagian 3 — Catatan Implementasi (20 Agustus 2026) ✅ SELESAI

Dikerjakan mengikuti Lampiran D, dengan tiga penyesuaian yang muncul saat menulis kodenya —
semuanya dari mengukur interaksi antar-komponen yang sesungguhnya, bukan dari menebak.

**D2.2 diterapkan tanpa keadaan `checked` yang direncanakan.** `ToolStripHoverButton` (komponen
bersama, dipakai di seluruh tool strip) mengambil alih properti `checked` untuk berarti "drop panel
sedang terbuka" begitu `dropPanelComponent` disetel — dan menimpa binding deklaratif ke
`toolStripAction.checked` dengan nilai polos pada klik pertama. `checked` yang disetir dari
`armedTool` akan basi setelah satu kali pakai. Solusinya: teks tombol sendiri yang jadi indikator
("Plan" → "Tap: Waypoint" / "Tap: ROI"), karena `text` tetap binding sungguhan sepanjang waktu.
Ditemukan dengan membaca `ToolStripHoverButton.qml` sebelum menulis `LocalGridPlanAction.qml`, bukan
setelah gagal.

**Ketegangan antara "klik kosong menutup editor" dan "klik kosong mereset titik sisip" diselesaikan
memilih yang kedua secara konsisten.** D3 Lampiran D butir 6 sempat menyiratkan panel klik bisa
menyisip di tengah bila seleksi sebelumnya dibiarkan hidup lewat klik kosong. Diuji ulang: perilaku
itu berarti dua concern berbeda (menutup tampilan vs mengubah titik sisip terkomit) bercampur di satu
`clearWaypointSelection()`, dan operator yang menutup editor untuk sekadar melihat grid akan diam-diam
mengubah ke mana klik *berikutnya* mendarat. **Keputusan: panel klik tetap selalu menyisip di akhir**
(perilaku sebelum Bagian 3, sesuai janji "tidak ada perubahan perilaku" di D3 butir 6) — mid-plan
insert hanya lewat senjata aktif dan menu baris. Catatan mid-plan yang disebut D3 butir 6 karena itu
tidak jadi ditambahkan ke `LocalGridClickPanel.qml`: dengan keputusan ini ia tidak akan pernah
terlihat.

**Stub `MissionController` di test butuh penyisipan berbasis indeks sungguhan, bukan sekadar
`push`.** Stub lama (dipakai ~40 tes) selalu `list.push(...)` terlepas dari parameter `index` —
cukup selama grid selalu menyisip di `-1`. Diperluas: `_insertAt()` yang benar-benar `splice` pada
posisi, `setCurrentPlanViewSeqNum()` yang mencari lewat kecocokan `sequenceNumber` persis seperti
`MissionController.cc:1943` sungguhan, dan `addItem`/`addHomeItem` yang menyetel
`currentPlanViewVIIndex` ke item yang baru ditambahkan — meniru bahwa `_initAllVisualItems` yang asli
selalu menutup dengan memanggil `setCurrentPlanViewSeqNum` terhadap item terakhirnya sendiri. Tanpa
bagian terakhir ini satu tes lama (`_homeItemIsNotDrawnAsAWaypoint_test`) gagal: takeoff otomatis
tersisip *sebelum* item home alih-alih sesudahnya, karena `currentPlanViewVIIndex` stub diam di -1
selamanya begitu `addHomeItem` dipanggil langsung tanpa lewat jalur insert.

**Satu asersi tes lama diperbarui, bukan dihapus.** `_waypointPlacedInMetres_reachesThePlanAsACoordinate_test`
menegaskan `lastIndex === -1` ("append"). Dengan Bagian 3, titik sisip sekarang `currentPlanViewVIIndex + 1`
— pada plan kosong itu jatuh di indeks 1 (tepat sesudah takeoff yang baru disisipkan otomatis), bukan
-1. Nilainya diganti dan komentarnya dijelaskan ulang; perilakunya sendiri (mendarat tepat sesudah
takeoff) tidak berubah.

**Cakupan yang sengaja dipersempit dari D4:** butir 4 (tombol "Waypoint" mati saat
`flyThroughCommandsAllowed` false) tidak mendapat tes tersendiri — itu binding satu arah yang lurus
di `LocalGridPlanAction.qml` tanpa logika baru di sisi grid, dan risikonya rendah. Sepuluh tes yang
direncanakan D4 diganti sembilan yang lebih tepat sasaran (butir "sisip di tengah" dan "editor
terbuka pada item baru" dipisah jadi dua tes, bukan satu), ditambah dua tes baru untuk
`duplicateItem`/`insertWaypointBetween` (D2.6/D2.7) yang tidak eksplisit disebut D4 tapi adalah
logika baru yang sama pentingnya.

**Verifikasi:** `LocalGridViewTest` 69/69 lulus (9 baru + 60 lama, termasuk dua yang sempat merah
saat pertama ditulis — salah nilai uji karena `Fact` bertipe int32 secara default, bukan bug
implementasi; diperbaiki dengan mengganti 7.5/2.5 jadi 8.0/3.0). `ctest -R
"LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` 15/15 lulus.
`LocalGridResponsiveLayoutTest` lulus tanpa anggaran chrome disentuh. `ctest -L Unit` penuh:
215/218 — tiga kegagalan sama persis yang sudah didokumentasikan sebelum pekerjaan ini
(`BluetoothConfigurationTest`, `BluetoothWorkerTest`, `LoggingQmlBindingTest`), tidak satu pun
baru. `qmllint` pada seluruh berkas yang disentuh: hanya dua kategori peringatan
(`unqualified`, `unresolved-type`) yang sudah tersebar di seluruh basis kode akibat qmllint
dijalankan tanpa jalur impor build — berkas baru `LocalGridPlanAction.qml` nol peringatan.
Plan view tidak disentuh sama sekali (`git diff` tidak menyentuh `PlanView.qml` atau
`MissionController.*`), sesuai D5.

---

## Lampiran E — Briefing Eksekusi Bagian 4

Ditulis setelah memeriksa apa yang benar-benar dilakukan ArduCopter terhadap tiap field yang
diusulkan, ke source pada checkout `/Users/mc/CLionProjects/ardupilot` (master,
`ArduPilot-4.6.0-beta1-8092-g2cd33afd17`) — standar bukti **S** yang sama dipakai panduan parameter.

Hasilnya memangkas Bagian 4, bukan memperluasnya. Kriteria "tidak ada field hiasan" ternyata
menggugurkan **empat dari tujuh** hal yang disebut rencana.

### E0 — Yang digugurkan, dan buktinya

**1. Acceptance radius per-waypoint tidak sampai ke pesawat.** Ini temuan terpenting di sini, dan
persis kelas kesalahan yang sama dengan altitude landing yang sudah dibereskan sebelumnya.

`AP_Mission` menyimpan tiap item misi dalam rekaman 15 byte yang tidak muat menampung delay **dan**
radius sekaligus. Untuk build **non-Plane** — yaitu Copter — dekodernya hanya menyimpan param1:

```cpp
// AP_Mission.cpp:1092-1116
case MAV_CMD_NAV_WAYPOINT: {
    /*
      the 15 byte limit means we can't fit both delay and radius
      in the cmd structure. ...
     */
#if APM_BUILD_TYPE(APM_BUILD_ArduPlane)
    ... acp / passby disimpan ...
#else
    cmd.p1 = (uint16_t)packet.param1;   // hanya ini
#endif
}
```

Jadi **param2 (acceptance radius) dan param3 (pass-by) dibuang saat upload**. Yang sungguh-sungguh
menentukan kapan sebuah waypoint dianggap tercapai adalah parameter kendaraan `WP_RADIUS_M`
(`AC_WPNav.cpp:65`; dulu bernama `WPNAV_RADIUS`, ada di tabel konversi `AC_WPNav.cpp:142`).
Menampilkan field per-item untuknya berarti menawarkan angka yang tidak akan pernah diterbangkan.

**2. Yaw per-waypoint juga dibuang.** Blok dekode yang sama tidak menyentuh `param4` sama sekali.
Cara Copter yang sungguhan untuk menyetir hidung di dalam misi adalah item tersendiri,
`MAV_CMD_CONDITION_YAW` (`mode_auto.cpp:791`) — bukan field pada waypoint.

**3. Aksi akhir misi adalah kode mati di QGC.** `MissionSettingsItem::addMissionEndAction()`
(`MissionSettingsItem.cc:147`) mengabaikan ketiga argumennya dan `return false`. Tidak ada apa pun
di baliknya. UI untuknya akan jadi field hiasan paling murni yang bisa dibuat.

**4. Cruise/hover speed di mission settings menjadi mubazir setelah Bagian 3.** Grid sudah menulis
`speedSection` pada **setiap** waypoint yang ditaruhnya, dan kalkulator statistik membaca yang
per-item itu (E1 di bawah). Menambah satu speed tingkat-misi berarti dua tempat menyatakan hal yang
sama, dengan yang belakangan menang secara diam-diam.

### E1 — Yang lolos, dan buktinya

**1. Hold time (`param1`) benar-benar diterbangkan.** `do_nav_wp` menyimpannya
(`mode_auto.cpp:1584`, `loiter_time_max = cmd.p1`) dan `verify_nav_wp` menahan pesawat sampai
detiknya habis. Satuannya **detik**, bilangan bulat (`uint16_t`).

Nilainya untuk riset ini bukan sekadar paritas: hover diam di satu titik adalah cara mengukur drift
optical flow **tanpa** komponen jarak tempuh — kondisi eksperimen yang berbeda dari terbang menerus,
dan sampai sekarang tidak bisa dinyatakan dari grid sama sekali.

**2. Statistik misi dihitung juga di fly view.** `_recalcMissionFlightStatus`
(`MissionController.cc:1160`) **tidak** digerbangi `_flyView` — berbeda dari jebakan
`_setPlannedHomePositionFromFirstCoordinate` yang ditemukan di Bagian 3. Dan kalkulatornya membaca
kecepatan per-item:

```cpp
// MissionFlightStatusCalculator.cc:226
double newSpeed = item->specifiedFlightSpeed();
```

yaitu persis yang ditulis grid ke tiap waypoint. Jadi jarak dan waktu untuk plan yang dibangun di
grid ini **akurat**, bukan perkiraan kasar dari parameter kendaraan.

**Satu kehalusan yang harus dinyatakan:** kalkulator itu tidak punya suku hold time sama sekali.
Begitu hold time bisa disetel (E1.1), waktu yang ditampilkan akan kurang sebesar jumlah seluruh
hold. Panel statistik harus menambahkannya sendiri, atau mengatakan bahwa ia tidak menghitungnya —
angka yang diam-diam kurang lebih buruk daripada angka yang tidak ada.

### E2 — Keputusan

**E2.1 — Bagian 4 jadi tiga hal, bukan tujuh.**

| Yang dikerjakan | Kenapa |
|---|---|
| Field **hold time** pada waypoint | Satu-satunya param NAV_WAYPOINT yang selamat sampai pesawat, dan ia membuka kondisi eksperimen baru (drift saat diam) |
| **Item `CONDITION_YAW`** | Tanpa GNSS, heading adalah satu-satunya rujukan absolut. Ini cara Copter yang benar untuk menyatakannya di dalam misi |
| **Panel statistik misi** | Sudah akurat untuk plan grid, tinggal ditampilkan — dengan hold time dijumlahkan sendiri |

**E2.2 — Tidak ada pemilih MAV_CMD umum.** Rencana menawarkan "pemilih perintah yang lebih luas
(subset yang masuk akal tanpa GNSS)", dengan catatan naikkan ke Opus bila dikerjakan. Setelah E0,
subset itu ternyata hampir kosong: sebagian besar perintah yang bisa dipilih membawa parameter yang
tidak selamat di rekaman 15 byte, atau bergantung pada GNSS/kamera yang tidak ada di wahana ini.
Yang tersisa bernilai cuma satu, `CONDITION_YAW`, dan itu lebih jujur ditawarkan sebagai jenis item
tersendiri di panel Plan daripada disembunyikan di balik pemilih kategori.

Konsekuensi biaya: **Bagian 4 tetap `sonnet` / `medium`.** Syarat "naikkan ke Opus" di rencana
tergantung pemilih perintah, dan pemilih perintah tidak jadi dikerjakan.

**E2.3 — Kamera ditunda, bukan ditolak.** `cameraSection` punya gerbang `available` sendiri, jadi
menampilkannya aman. Tapi wahana riset ini tidak membawa kamera atau gimbal, jadi tidak ada yang
bisa membuktikan field itu bekerja — dan field yang tidak bisa diuji adalah field yang tidak bisa
dijamin. Ditunda sampai ada perangkat yang membuatnya terbukti.

**E2.4 — Field hold time menulis `param1` lewat `textFieldFacts`, bukan lewat Fact baru.**
`SimpleMissionItem` sudah menerbitkan `textFieldFacts` (`SimpleMissionItem.h:43`) yang berisi
`_param1Fact` untuk perintah yang memakainya, dengan nama dan satuan dari command tree QGC. Membuat
Fact sendiri berarti dua sumber untuk satu angka.

### E3 — Yang dikerjakan

**1. `LocalGridView.qml`**

- `waypointHoldTimeFact(index)` — kembalikan `_param1Fact` hanya untuk item ber-`commandWaypoint`,
  null untuk yang lain (takeoff dan landing memakai param1 untuk hal berbeda).
- `missionHoldSeconds` — jumlah seluruh hold time di plan, untuk dipakai panel statistik.
- `insertConditionYawAt(...)` / dukungan tipe baru pada `addMissionItemAt`.
- Konstanta `commandConditionYaw: 115`, ditulis keluar seperti tiga konstanta lain yang sudah ada,
  dan dipin ke header MAVLink oleh tes.

**2. `LocalGridWaypointEditor.qml`** — satu `EntryRow` hold time, hanya untuk waypoint biasa, dengan
satu kalimat yang menyebut bahwa radius penerimaan datang dari `WP_RADIUS_M` dan bukan dari sini.
Kalimat itu bukan hiasan: ia mencegah operator mencari field yang sengaja tidak ada.

**3. `LocalGridMissionStats.qml` (baru)** — jarak total, waktu (termasuk hold), jumlah item. Dilipat
seperti panel lain, dan mengikuti aturan `compact` Bagian 2 supaya anggaran chrome tidak naik.

**4. `LocalGridPlanAction.qml`** — satu tombol lagi di drop panel untuk `CONDITION_YAW`.

### E4 — Tes

1. Hold time yang diketik sampai ke `param1` item, dan hanya muncul untuk waypoint biasa.
2. `missionHoldSeconds` menjumlahkan seluruh hold, dan nol untuk plan tanpa hold.
3. Statistik menampilkan jarak yang cocok dengan pola kotak yang diketahui (4 × 20 m = 80 m).
4. Waktu statistik = waktu terbang + hold, bukan waktu terbang saja.
5. `CONDITION_YAW` tersisip di titik yang benar dan **tidak** ikut digambar sebagai leg (ia tidak
   membawa koordinat sama sekali — jalur yang sama dengan Cancel ROI di Bagian 3).
6. Anggaran chrome `LocalGridResponsiveLayoutTest` tidak naik di keempat ukuran.

### E5 — Selesai bila

- Tiap field yang tampil bisa ditunjuk barisnya di source ArduPilot yang membacanya
- Enam tes di atas hijau
- `LocalGridResponsiveLayoutTest` lulus tanpa anggaran chrome disentuh
- `ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` hijau

---

## Bagian 4 — Catatan Implementasi (20 Agustus 2026) ✅ SELESAI

Dikerjakan mengikuti Lampiran E. Tiga hal yang dikerjakan tetap tiga; yang berubah adalah **cara**
sebuah parameter ditemukan pada item, dan itu berubah karena membaca sisi QGC-nya, bukan hanya sisi
ArduPilot-nya.

**Parameter dicari lewat nama, bukan lewat posisi — dan di dua daftar sekaligus.** E2.4 benar bahwa
`textFieldFacts` adalah sumber yang tepat, tapi tidak menyebut bahwa daftar itu **terfilter dan
terbelah dua**. `SimpleMissionItem::_rebuildTextFieldFacts` menamai tiap fact dengan label dari
command tree (`SimpleMissionItem.cc:451`, `paramFact->setName(paramInfo->label())`) lalu
menaruhnya di `_textFieldFactsAdvanced` bila param itu ditandai `advanced`, dan di `_textFieldFacts`
bila tidak (`:468-470`). `Hold` ditandai advanced di `MavCmdInfoCommon.json`; `Heading` milik
`CONDITION_YAW` tidak. Jadi `_namedFactOf()` menyapu **kedua** daftar dan mencocokkan nama.

Indeks tidak dipakai karena isi daftar itu bergantung firmware: `APM-MavCmdInfoCommon.json` membuang
param2 dari `NAV_WAYPOINT` dan `APM-MavCmdInfoMultiRotor.json` membuang param3 dan param4. Ini
sekaligus **bukti kedua, di sisi QGC**, untuk E0 butir 1 dan 2 — QGC sendiri sudah tidak menawarkan
acceptance radius maupun yaw per-waypoint untuk multirotor ArduPilot. Temuan Lampiran E dari sisi
`AP_Mission.cpp` dan keputusan upstream QGC sampai pada tempat yang sama.

**`CONDITION_YAW` lolos jalur friendly-edit hanya karena punya objek param.** JSON-nya tidak menulis
`friendlyEdit` sama sekali, dan defaultnya `false` (`MissionCommandUIInfo.cc:313-315`) — yang akan
melempar item ini ke jalur `rawEdit`, tempat fact-nya dinamai `Param1`..`Alt/Z` dan pencarian per
nama pasti gagal. Yang menyelamatkannya satu baris: `_setInfoValue(_friendlyEditJsonKey, true)`
dipanggil begitu ada objek param yang dibaca (`:370`). Perlu dicatat karena ini bukan jaminan yang
dinyatakan di mana pun — kalau param `CONDITION_YAW` suatu saat dipangkas habis oleh override
firmware, field Heading hilang diam-diam. Yang aman: hilang, bukan salah edit.

Param3 (`Direction`) dan param4 (`Offset`) punya `enumStrings`, jadi keduanya memang tidak pernah
masuk `textFieldFacts` (`SimpleMissionItem.cc:447`). Yang terbit hanya `Heading` dan `Rate`.

**Derajat: grid menulis 0–360, command tree menampilkan −180..180.** Itu `userMin`/`userMax`, yaitu
batas validasi UI, bukan `min`/`max` metadata — jadi `rawValue` 270 diterima tanpa penolakan.
Karena field bawaan akan menandai 270 sebagai di luar rentang, baris heading di editor memakai
`EntryRow` grid sendiri (yang sudah dipakai bearing/jarak) dan bukan `FactTextField`, dengan
pembungkusan `((x % 360) + 360) % 360` supaya 370 berarti 10.

**Penyisipan yaw menempuh jalur Cancel ROI, dan namanya `insertConditionYaw` (bukan
`insertConditionYawAt` seperti di E3).** `MissionController` tidak punya penyisip khusus untuk
perintah ini, jadi item disisipkan sebagai simple item lalu diberi command-nya — persis pola yang
sudah dipakai Bagian 3. Tombolnya di drop panel "Plan" menyisipkan pada **heading kendaraan saat
itu**, bukan nol: yang hampir selalu dimaksud operator adalah mengunci hidung di arah sekarang, dan
default nol berarti "putar ke utara" yang akan mengayun wahana pada run pertama.

Tombolnya digerbangi `flyThroughCommandsAllowed` seperti tombol Waypoint di sebelahnya, meski
`CONDITION_YAW` bukan perintah fly-through. Itu pemakaian ulang yang disengaja: flag itu tepatnya
berarti "sebelum takeoff atau sesudah land" (`MissionController.cc:2081` dan `:2096`), dan di kedua
tempat itu sebuah item yaw memang tidak berarti apa-apa — yang sesudah land tidak pernah dijalankan,
dan yang sebelum takeoff melanggar syarat takeoff harus item pertama.

**Stub takeoff di test tidak pernah membawa command-nya sendiri.** `insertTakeoffItem` pada stub
menyetel `isTakeoffItem` dan `commandName` tapi membiarkan `command` di 16 (`NAV_WAYPOINT`) — tidak
pernah terlihat karena belum ada yang bertanya. Tes hold time langsung menemukannya: field itu
muncul di takeoff. Diperbaiki jadi `command: 22`. Ini bug stub, bukan bug implementasi, dan
memperbaikinya membuat 69 tes lama tetap hijau.

**Batas yang jujur soal tes yaw.** `Fact` menerbitkan `name` sebagai properti read-only, jadi QML
tidak bisa menamai fact-nya sendiri dan stub tidak bisa meniru penerbitan fact yang dilakukan
command tree saat command ditulis. Fact diterbitkan dari sisi C++ (`publishItemFact`). Akibatnya
yang teruji adalah: item tersisip di posisi yang benar dengan command yang benar, tidak digambar dan
tidak ikut diukur sebagai leg, lalu `setWaypointYawHeading`/`waypointYawHeading` bekerja dengan
konvensi 0–360 termasuk pembungkusan. Yang **tidak** teruji di stub: heading yang disetel oleh
`insertConditionYaw` sendiri pada detik penyisipan, karena pada stub fact-nya belum ada saat itu.
Pada controller sungguhan fact itu ada, karena `setCommand` membangun ulang fact-nya seketika.

**Empat tes, bukan enam.** Butir 3 dan 4 di E4 (jarak pola kotak, dan waktu = terbang + hold) adalah
dua asersi atas satu plan yang sama, jadi digabung ke `_planTotalsIncludeTheWaits_test` — yang
sekalian menegaskan teks yang benar-benar tampil di panel (jarak lewat transform yang sama sehingga
tetap benar di satuan non-metrik, waktu sebagai `1:30`, dan catatan hold yang muncul hanya kalau ada
hold). Butir 6 (anggaran chrome) tidak butuh tes baru: `LocalGridResponsiveLayoutTest` sudah
mengukurnya dan tetap hijau tanpa anggarannya disentuh.

**Verifikasi:** `LocalGridViewTest` 73/73 lulus (4 baru + 69 lama). `ctest -R
"LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` 15/15 lulus, termasuk
`LocalGridResponsiveLayoutTest` tanpa anggaran chrome disentuh. `ctest -L Unit` penuh: 215/218 —
tiga kegagalan yang sama persis dengan yang sudah didokumentasikan di §5 sebelum pekerjaan ini
(`BluetoothConfigurationTest`, `BluetoothWorkerTest`, `LoggingQmlBindingTest`), tidak satu pun
baru. `qmllint` pada berkas yang disentuh:
hanya dua kategori peringatan yang sudah tersebar di seluruh basis kode (`unqualified`,
`unresolved-type`) akibat qmllint dijalankan tanpa jalur impor build — `LocalGridMissionStats.qml`
yang baru menghasilkan pola peringatan yang sama persis dengan panel tetangganya
(`LocalGridReadout.qml`), bukan yang baru. `pre-commit run --files` atas tujuh berkas yang disentuh:
seluruh hook yang relevan hijau — termasuk **Lint QML files** (qmllint dengan jalur impor build,
nol peringatan), `typos`, `QTest::ignoreMessage`, dan `qWait` berdelay tetap. Tiga hook merah,
tidak satu pun karena isi perubahan ini: `clang-format` (22.1.8 lokal vs 22.1.5 yang di-pin)
menandai seluruh berkas tes dari baris pertama termasuk kode yang tidak disentuh, sesuai catatan
di §5 bahwa basis repo memang belum format-clean; `cmake-format` dan `cmake-lint` mati dengan
`ModuleNotFoundError: No module named 'yaml'` dari environment hook-nya sendiri (python3.14) —
gagal sebelum sempat membaca berkas mana pun. Plan view tidak disentuh
(`MissionController.*` dan `PlanView.qml` tidak ada di diff), sesuai E5.

---

## Lampiran F — Briefing Eksekusi Bagian 5

Ditulis setelah memeriksa apa yang benar-benar dilakukan `rotateMission`/`offsetMission` milik QGC
dan apa yang sudah ada di grid hari ini. Standar bukti **S** yang sama: tiap klaim menunjuk baris.

Hasilnya: kalimat rencana "`rotateMission`/`offsetMission` sudah tersedia" **benar untuk geser dan
menyesatkan untuk putar**.

### F0 — Keadaan terukur hari ini

**1. Geser seluruh misi sudah ada, dan sudah ada di grid sendiri.** `LocalGridView.qml:1330`
(`offsetMission(northMetres, eastMetres)`) dipakai oleh koreksi posisi
(`LocalGridPositionCorrection.qml:126`) dan oleh `reanchorPlanToVehicle`. Ia berjalan di atas
snapshot `missionPoints`, melewati item pinned dan item yang tidak ada di grid, dan menulis lewat
`moveWaypointTo`. **Yang belum ada** adalah cara operator menggeser pola dengan sengaja sekian meter
— yang ada hanya dua jalur yang dimaksudkan sebagai koreksi drift.

**2. `MissionController::rotateMission` ada, tapi tidak bisa dipakai apa adanya dari fly view.**
Tiga alasan, masing-masing cukup sendirian:

- **Porosnya planned home**, bukan origin: `const QGeoCoordinate home = _settingsItem->coordinate();`
  lalu tiap item diputar terhadapnya (`MissionController.cc:2242`, `:2260-2266`).
- **Home yang tidak sah membuatnya diam.** `MissionController.cc:2237-2240` hanya menulis
  `qCWarning` lalu `return` — dari sisi operator itu tombol yang tidak melakukan apa pun dan tidak
  mengatakan apa pun. Plan view menutupi ini dengan menggerbangi tombolnya pada `_hasHome`
  (`TransformEditor.qml:332`, "Home position must be set to rotate the mission"); grid harus
  melakukan hal setara atau tidak memakai jalur ini sama sekali.
- **Di fly view home tidak pernah diturunkan dari pola.** `_setPlannedHomePositionFromFirstCoordinate`
  digerbangi `!_flyView` (`MissionController.cc:1441`). Yang menyetelnya di fly view cuma dua: item
  home dari kendaraan saat plan diunduh (`:133-139`), atau takeoff yang ditulis grid — karena
  takeoff ArduPilot multirotor `specifiesCoordinate()`-nya false, `_launchTakeoffAtSameLocation`
  jadi true (`TakeoffMissionItem.cc:129-131`) dan `setCoordinate` ikut menulis home
  (`TakeoffMissionItem.cc:99-104`). Jadi poros yang sama bisa berarti origin, bisa berarti home
  kendaraan, bisa berarti tidak ada — tiga arti untuk satu tombol.

**3. Rotasi controller geodesik terhadap home** (`home.distanceTo` / `home.azimuthTo`, `:2260` dan `:2264`),
sementara grid berbicara meter north/east dan punya proyeksinya sendiri.

**4. Controller tidak tahu apa-apa tentang item `CONDITION_YAW`.** Ia melewati tiap item yang
`!specifiesCoordinate()` (`:2246`) — dan itu persis item yaw yang baru ditambahkan Bagian 4.
Memutar pola tanpa memutar heading-nya menghasilkan pola yang benar dengan hidung yang salah, dan
pada wahana tanpa GNSS arah hidung adalah bagian dari yang diukur, bukan detail tampilan.

### F1 — Keputusan

**F1.1 — Rotasi dikerjakan di grid, bukan didelegasikan.** Dalam bingkai north/east grid, lewat
`moveWaypointTo` (yang sudah menolak item pinned), berjalan di atas snapshot `missionPoints` —
alasan yang persis sama dengan `offsetMission`: `missionPoints` adalah binding atas koordinat item,
jadi daftar yang dihitung ulang di tengah jalan akan memutar item kedua dua kali.

**F1.2 — Porosnya jangkar plan, bukan origin dan bukan home.** `planAnchorNorth`/`planAnchorEast`
(`LocalGridView.qml:1375-1378`) menyimpan di mana pola *saat ini* dimulai. Untuk pola yang belum
pernah digeser nilainya (0, 0) — yaitu origin, yang juga tempat takeoff dipaku. Untuk pola yang
sudah dipindah ke posisi wahana, memutar terhadap origin akan menyapu seluruh pola mengelilingi
origin: titik awalnya pindah entah ke mana, sementara `reanchorNorthMetres` tetap dihitung dari
jangkar yang tersimpan, bukan dari titik yang sebenarnya. Memutar terhadap jangkar membuat titik
awal pola tetap di tempatnya dan jangkar tetap sahih tanpa disentuh.

**F1.3 — Heading item yaw ikut diputar sebesar sudut yang sama.** Ia tidak punya koordinat untuk
diputar; yang berputar adalah angkanya. `setWaypointYawHeading` sudah membungkus 0–360, jadi
penambahannya tidak perlu penjagaan sendiri.

**F1.4 — Geser sengaja memakai `offsetMission` yang sudah ada, tapi wajib memperbarui jangkar.**
Ini konsekuensi yang mudah terlewat: jangkar berarti "di mana pola dimulai", jadi geseran yang
disengaja menggeser artinya juga. Kalau jangkar tidak ikut, `planStartsAtVehicle` akan mengatakan
pola sudah mulai di wahana padahal ia dua meter dari situ, dan alasan tombol "Fly this plan from
here" jadi bohong. Rotasi tidak menyentuh jangkar (F1.2); geseran selalu menyentuhnya.

**F1.5 — Tidak ada checkbox "juga pindahkan takeoff/landing" ala Plan view.** Grid sudah punya
aturannya sendiri dan aturan itu lebih tegas: takeoff dipaku ke origin karena multirotor naik di
tempat, jadi ia tidak pernah ikut; sisanya — termasuk landing dan ROI — ikut, sehingga pola dan
titik yang dipandanginya tetap pada hubungan yang sama. Dua checkbox yang menawarkan pilihan yang
tidak masuk akal di grid ini hanya menambah dua cara untuk salah.

**F1.6 — Tidak ada panel baru.** Alat ini masuk sebagai satu kelompok di dalam
`LocalGridMissionActions`, dengan judul tebal seperti kelompok "After a flight" yang sudah ada
(`LocalGridMissionActions.qml:605-610`). Anggaran chrome Bagian 2 karena itu tidak berubah, dan
tidak perlu disentuh di `LocalGridResponsiveLayoutTest`.

**F1.7 — Gerbang yang sama dengan reanchor.** Tidak saat armed (memutar pola di bawah wahana yang
sedang menerbangkannya mengubah tujuannya di tengah terbang), tidak saat transfer berjalan, dan
hanya bila ada yang bisa diputar. Alasannya dinyatakan di sebelah tombol, bukan dibiarkan sebagai
tombol mati — aturan yang sudah dipakai `reanchorBlockedReason`.

### F2 — (b) Survey di atas poligon: dipecah jadi Bagian 5b dan ditunda

Rencana sudah menyediakan pintu ini ("kalau ternyata besar, pecah jadi Bagian 5b"). Ia besar, dan
ada satu alasan tambahan yang lebih menentukan daripada besarnya:

- **Poligonnya tidak punya editor di sini.** Plan view menggambar poligon lewat
  `QGCMapPolygonVisuals` di atas peta. Grid tidak punya peta; menambah/menggeser/menghapus verteks
  yang ramah sentuh adalah pekerjaan sebesar Bagian 3 sendiri, sebelum satu transek pun dihitung.
- **Item kompleks tidak pernah dibangun ulang saat plan datang dari kendaraan.** Yang dipindai hanya
  pola landing (`MissionController.cc:1782-1783`); tidak ada `scanForItem` untuk survey di jalur
  itu. Jadi survey yang sudah diunggah kembali sebagai deretan waypoint biasa — sekali round-trip ia
  berhenti menjadi survey dan tidak bisa disunting lagi. Digabung dengan aturan "fly view adalah
  cermin kendaraan" yang ditemukan Bagian 3, umur sebuah survey lokal sangat pendek.
- **Nilainya untuk riset ini kecil.** Pola kotak dan lawnmower di ruang uji berukuran belasan meter
  bisa dibuat dengan 4–8 klik di grid, dan sejak Bagian 3–4 tiap titiknya bisa disisipi, digandakan,
  diberi kecepatan, hold, dan heading. Survey membayar mahal untuk sesuatu yang sudah murah.

Kalau kelak dikerjakan, urutannya: editor poligon dulu sebagai bagian tersendiri, survey menyusul.

### F3 — Yang dikerjakan

**1. `LocalGridView.qml`**

- `rotatePlan(degreesCW)` — memutar tiap titik yang tidak dipaku dan ada di grid terhadap jangkar,
  lalu menambah `degreesCW` ke heading tiap item yaw. Mengembalikan jumlah item yang berubah.
- `nudgePlan(northMetres, eastMetres)` — `offsetMission` plus pemutakhiran jangkar (F1.4).
- `canTransformPlan` + `transformBlockedReason` — sepasang dengan `canReanchorPlan` /
  `reanchorBlockedReason`, dengan alasan yang sama persis kata-katanya di mana keadaannya sama.

**2. `LocalGridMissionActions.qml`** — kelompok "Shape the pattern": satu field derajat searah jarum
jam dengan tombol Apply, satu pasang field north/east dengan tombol Apply, satu baris alasan, satu
baris hasil ("N item(s) turned. Upload the plan to fly it.").

> **Dikoreksi saat pelaksanaan:** kelompok ini akhirnya masuk ke `LocalGridPlanAction.qml` (drop
> panel "Plan"), bukan ke panel aksi. Panel aksi ternyata sudah penuh — dua tes UI membuktikannya.
> Lihat "Bagian 5 — Catatan Implementasi".

### F4 — Tes

1. Memutar 90° CW memindahkan (20, 0) ke (0, 20) dan membiarkan takeoff di origin.
2. Memutar pola yang sudah dipindah berputar terhadap jangkarnya, bukan terhadap origin — titik awal
   pola tetap di tempat.
3. Heading item yaw ikut bertambah sebesar sudut yang sama, dan membungkus di 360.
4. Geser sengaja memperbarui jangkar: sesudahnya `planStartsAtVehicle` menjawab dengan jujur.
5. Ditolak saat armed dan saat transfer berjalan, dengan alasan yang tidak kosong.
6. Anggaran chrome `LocalGridResponsiveLayoutTest` tidak naik — tidak ada panel baru.

### F5 — Selesai bila

- Sebuah pola bisa diselaraskan ke dinding atau garis landas tanpa menyentuh satu pun waypoint
- Enam tes di atas hijau, `LocalGridViewTest` seluruhnya hijau
- `ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` hijau
- (b) tercatat sebagai Bagian 5b dengan alasannya, bukan hilang diam-diam

---

## Bagian 5 — Catatan Implementasi (20 Agustus 2026) ✅ SELESAI

Dikerjakan mengikuti Lampiran F tanpa perubahan keputusan. Yang bertambah adalah beberapa hal yang
baru kelihatan saat menulis kodenya dan tesnya.

**Poros jangkar terbukti bukan detail.** Tes memutar pola dua kali: sekali pada pola yang belum
digeser (poros = origin), sekali sesudah `nudgePlan(10, 0)`. Kasus kedua adalah yang membedakan
implementasi ini dari yang mudah: titik (10, 20) diputar 90° searah jarum jam menghasilkan **(-10, 0)**
terhadap jangkar dan **(-20, 10)** terhadap origin. Keduanya angka yang masuk akal di layar; hanya
satu yang membiarkan pola tetap dimulai di tempat yang sama. Asersi tesnya menyebut kedua angka itu
supaya kegagalannya membaca sendiri.

**Putaran satu putaran penuh dijawab "tidak ada yang berubah", bukan ditulis ulang.** `rotatePlan`
menormalkan sudut sebelum memutuskan (`((deg % 360) + 360) % 360 === 0`), jadi 360 dan -360 tidak
menyentuh satu item pun. Bukan optimasi: setiap penulisan koordinat menandai plan kotor dan
membangunkan seluruh binding yang menggantung padanya.

**Item yaw diperlakukan sebelum saringan `onGrid`, bukan sesudahnya.** Ia tidak punya koordinat, jadi
`onGrid`-nya false dan saringan yang sama yang melindungi takeoff akan melewatinya diam-diam —
menghasilkan pola yang berputar dengan hidung yang tidak. Urutan di dalam loop itu satu-satunya hal
yang membuat F1.3 benar-benar berlaku.

**Geser sengaja dan jangkar diuji lewat angka yang dilihat operator.** Tesnya memberi wahana posisi
5 m di utara, memastikan tawaran "pindahkan plan ke sini" berbunyi 5 m, lalu menggeser pola 2 m ke
utara dan menuntut tawaran itu menjadi 3 m. Itu invarian F1.4 dinyatakan dalam satuan yang muncul di
panel, bukan dalam nilai fact.

**F1.6 salah, dan tesnya yang membuktikannya.** Rencana menaruh alat pola sebagai satu kelompok di
dalam `LocalGridMissionActions` dengan alasan "tidak ada panel baru, jadi anggaran chrome tidak
berubah". Anggarannya memang tidak dilanggar oleh panel baru — ia dilanggar oleh panel yang sudah
ada yang jadi lebih besar: `LocalGridResponsiveLayoutTest` melaporkan **15,5% pada tablet terhadap
anggaran 15%**. Dan `LocalGridPositionCorrectionUITest` gagal berbarengan dengan cara yang lebih
tajam: tombol "Vehicle is on the origin" terdorong keluar dari badan panel yang bisa digulir, jadi
kliknya mendarat di grid dan tidak ada perintah yang terkirim. Dua tes itu mengatakan hal yang sama —
panel aksi sudah penuh.

**Alat pola pindah ke drop panel "Plan".** Ia tidak berdiri di layar, jadi biayanya nol selama
tertutup; dan tempatnya memang di sana: drop panel itu sudah jadi tempat pola dibangun sejak
keputusan D2.2 di Bagian 3, dan membentuk pola adalah lanjutan dari membangunnya. Satu perbedaan
perilaku yang disengaja dari tombol-tombol lain di panel itu: tombol Turn dan Move **tidak** menutup
panelnya. Menyelaraskan pola ke ruangan dilakukan dengan mata dan lebih dari sekali — seperempat
putaran, lihat gridnya, lalu sisanya.

**Tiga hal kecil di luar kode fitur, semuanya karena `pre-commit` kini bisa dijalankan lokal:**

1. `markdownlint` menemukan dua daftar tanpa baris kosong di atasnya di dokumen ini (satu di
   Lampiran E, satu di F3) — diperbaiki. Sisa peringatannya (94 × MD060 gaya pipa tabel, dan tiga
   lagi di bagian-bagian lama) sudah ada sebelum pekerjaan ini dan tidak disentuh.
2. `typos` menandai kata Indonesia sebagai salah eja (`lokal`, `poligon`, `fase`). Akar masalahnya
   ternyata lebih tua: `.typos.toml` sudah mendaftar `docs/ko/`, `docs/tr/` dan seterusnya di
   `[files] extend-exclude`, tapi daftar itu **tidak pernah berlaku** untuk berkas yang diserahkan
   pre-commit satu per satu — typos hanya menghormatinya dengan `--force-exclude`. Jadi
   `docs/tr/` pun masih dipindai selama ini (40 temuan). Diperbaiki dengan menambahkan flag itu di
   `.pre-commit-config.yaml` dan mendaftarkan `docs/nongps/` bersama direktori bahasa lain.
   Auto-fix tetap mati — itu `--write-changes`, yang tidak ikut diberikan; diperiksa ulang bahwa
   salah eja Inggris yang sungguhan masih tertangkap.
3. Hook `Lint QML files` (qmllint dengan jalur impor build) hijau untuk kedua berkas QML yang
   disentuh.

**Verifikasi:** `LocalGridViewTest` 77/77 lulus (4 baru + 73 lama). `ctest -R
"LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` 15/15 lulus, termasuk
`LocalGridResponsiveLayoutTest`, `LocalGridPositionCorrectionUITest` dan `FlyViewLocalGridUITest` —
ketiganya sempat merah pada penempatan pertama dan hijau lagi sesudah alat pola dipindah ke drop
panel; anggaran chrome kembali tidak tersentuh karena alat itu tidak berdiri di layar. `ctest -L
Unit` penuh: 215/218 — tiga kegagalan yang sama dengan yang sudah didokumentasikan di §5
(`BluetoothConfigurationTest`, `BluetoothWorkerTest`, `LoggingQmlBindingTest`), tidak satu pun
baru. `pre-commit` atas berkas yang
disentuh: semua hijau kecuali `clang-format`, yang menandai seluruh berkas tes dari baris pertama
karena versi lokal berbeda dari yang di-pin (lihat §5). `PlanView.qml` dan `MissionController.*`
tidak disentuh.

---

## Lampiran H — Briefing Eksekusi Bagian 6b (Urungkan)

### H0 — Apa yang masuk riwayat: aturan satu-ketukan

Grid ini sudah punya gerbang untuk aksi yang mahal: `_confirm()` di `LocalGridMissionActions.qml:97`
membungkus Clear (`:134`), Start From The Beginning (`:270`), Fly From Here (`:308`), Download
(`:326`) dan Upload (`:389`). Semuanya menanyakan dulu.

Maka garisnya bukan "aksi mana yang penting", melainkan **aksi mana yang sudah dijaga**:

> Aksi yang sudah lewat dialog konfirmasi **tidak** masuk riwayat — ia sudah punya penjaganya.
> Aksi yang jadi hanya dengan satu ketukan atau satu seretan, tanpa penjaga apa pun, **masuk**.

| Masuk riwayat | Kenapa |
|---|---|
| Menaruh waypoint (panel klik, senjata aktif, tekan-lama) | Satu ketukan, langsung jadi |
| Menggeser waypoint (seret atau ketik) | Satu seretan; justru kegagalan yang 6a kurangi tapi tidak hilangkan |
| Menghapus item | Satu ketukan pada ikon tong sampah, tanpa konfirmasi |
| Duplikat, sisip-di-antara | Satu ketukan |
| Putar / geser seluruh misi | Satu ketukan, dan mengubah **setiap** item |
| "Set this altitude on all" / "Set this speed on all" | Satu ketukan dengan jangkauan terluas di seluruh grid |

| Tidak masuk | Kenapa |
|---|---|
| Clear, Restart, Fly From Here, Download, Upload | Sudah dikonfirmasi |
| Ketik altitude/speed/hold/heading pada satu item | Disengaja, dan nilai lamanya masih terbaca di field yang sama |

### H1 — Sedalam apa: satu tingkat

Rencana menyebut "penempatan/geser **terakhir**", dan itu ditepati. Alasan yang lebih kuat daripada
sekadar mengikuti kata-katanya: pengendali misi di fly view adalah **cermin kendaraan**, dan ia
membangun ulang seluruh itemnya setiap satu transaksi plan selesai. Setiap entri riwayat karena itu
hanya sah sampai plan berikutnya tiba. Satu entri yang dibatalkan dengan agresif bisa dibuktikan
benar; sebuah tumpukan adalah sekumpulan klaim tentang plan yang mungkin sudah tidak ada.

### H2 — Bentuk penyimpanan: aksi kebalikan, bukan snapshot

Dua jalur snapshot ditolak, dengan alasan terukur:

- **Snapshot dari sisi QML atas apa yang grid ketahui** — diam-diam membuang apa pun di luar
  kosakata grid (camera section, item kompleks, perintah dari berkas). Memulihkan plan begini
  merusak data tanpa bilang apa-apa.
- **Snapshot JSON lewat `MissionController::save`/`load`** — lengkap dan tidak merusak, tapi keduanya
  bukan `Q_INVOKABLE` (`MissionController.h:235-236`) jadi butuh tambahan C++ pada kode bersama; dan
  `PlanMasterController` hanya menerbitkan save/load **berbasis berkas** ke QML
  (`PlanMasterController.h:86,94`). Menulis berkas tiap kali satu waypoint ditaruh adalah bentuk yang
  salah — dan mesin ini baru saja membuktikan ruang disk bukan barang gratis.

**Dipilih:** tiap entri adalah aksi kebalikan yang dicatat pada saat aksinya dilakukan. Semua aksi di
H0 punya kebalikan yang eksak dan murah, kecuali satu:

| Aksi | Kebalikannya |
|---|---|
| taruh / duplikat / sisip-di-antara | hapus item itu |
| geser | kembalikan ke north/east yang dicatat |
| putar / geser misi | putar / geser dengan negasinya |
| set-altitude-on-all / set-speed-on-all | kembalikan nilai per item yang dicatat sebelumnya |
| hapus | bangun ulang dari deskripsi yang dicatat sebelumnya |

Hapus satu-satunya yang membangun ulang, dan ia hanya membangun ulang apa yang grid **bisa** jelaskan
seluruhnya: perintah, north/east, altitude beserta frame-nya, speed section, hold.

**Item yang tidak bisa dijelaskan seluruhnya tidak masuk riwayat saat dihapus** — dan kontrol undo
tidak muncul untuknya, alih-alih menawarkan pemulihan yang akan kembali berbeda dari aslinya. Batasnya
dibuat tegas dan mudah diuji: hanya perintah yang grid sendiri bisa buat (waypoint, takeoff, land,
ROI, cancel ROI, condition yaw), dan hanya bila item itu tidak membawa camera section yang aktif.

### H3 — Pembatalan riwayat (pelajaran Bagian 3)

Entri dibuang saat:

- **`visualItemsReset`** — plan tiba utuh dari kendaraan. Sinyal yang sama yang sudah membersihkan
  seleksi di Bagian 3, dan untuk alasan yang sama: entri itu menjelaskan plan yang sudah tidak ada.
- **`planSyncInProgress` menjadi true** — sama seperti penempatan ditolak saat itu.
- **item sasarannya sudah tidak ada** pada indeks yang dicatat.

### H4 — Di mana kontrolnya

Undo harus satu ketukan tepat setelah kecelakaannya, jadi ia tidak boleh berada di dalam panel yang
terlipat secara bawaan di layar sempit — panel `missionActions` mulai terlipat saat `compact`, dan
undo yang terlipat bukan undo.

**Keputusan:** kontrol mengambang kecil yang **hanya ada selama ada yang bisa diurungkan**. Biaya
anggaran chrome nol pada keadaan bawaan yang diukur tes anggaran — karena pada keadaan itu tidak ada
yang bisa diurungkan dan kontrolnya tidak ada.

Ia juga menyebut apa yang akan diurungkan ("Undo move", "Undo delete"), bukan cuma "Undo": satu
ketukan setelah sebuah kecelakaan, operator belum tentu tahu aksi mana yang tercatat terakhir.

### H5 — Tes

1. Taruh lalu urungkan: plan kembali seperti semula, dan kontrolnya hilang sesudahnya.
2. Geser lalu urungkan: waypoint kembali ke offset yang dicatat.
3. Hapus lalu urungkan: perintah, posisi, altitude, speed dan hold semuanya kembali.
4. Putar lalu urungkan: offset setiap item kembali.
5. Set-altitude-on-all lalu urungkan: altitude per item yang tadinya berbeda kembali berbeda.
6. Plan tiba dari kendaraan: entri dibuang, kontrol tidak ditawarkan.
7. Satu tingkat saja: dua aksi lalu urungkan hanya membatalkan yang kedua.
8. Anggaran chrome tidak naik — kontrol tidak ada saat tidak ada yang bisa diurungkan.

### H6 — Selesai bila

- Delapan tes di atas hijau, dan tes 1–3 terbukti merah dulu
- `LocalGridResponsiveLayoutTest` lulus tanpa anggaran chrome disentuh
- Ikon hapus baris naik ke ambang sentuh (G1) di bagian yang sama
- `ctest -R "LocalGrid|SetEstimatorOrigin|NonGps|FlyViewLocalGrid|MissionController"` hijau

---
