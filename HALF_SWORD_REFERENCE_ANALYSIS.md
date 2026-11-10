# Half Sword referans analizi ve Aedir sorun kökleri (Kasım 2026)

Bu belge kullanıcının 10 Kasım 2026'da gönderdiği görsellerden yola çıkıyor:
- Aedir'den 6 hata karesi
- Half Sword'dan 29 referans karesi: 9 normal tutuş, 12 yarım kılıç (half-swording) ve 8 ek kare

Her konu için üç şey var:
- referansta ne görüldüğü,
- bizde ne olduğu,
- koddaki kök neden ve yapılacak iş.

Araştırma sonuçları (web ve kod) ilgili bölümlere eklendi. `HALF_SWORD_COMBAT.md`'deki eski tespitlerin bir kısmı burada düzeltiliyor.

## 1. Referans pozlar: normal iki elli tutuş

Konumlar karakterin kendi yönüne göre verilmiştir: sağ ve sol karakterin sağı ve solu, ön de karakterin önü.

| # | Durum | Eller | Bıçak | Ayaklar / gövde |
|---|---|---|---|---|
| N1 | Fare bırakılmış, normal duruş 1 | İki el birlikte kasık önünde, dirsekler hafif bükülü, kollar gevşek aşağıda | Çapraz aşağı, sağ öne. Uç yere yakın, ~40° aşağıda, rakibe dönük değil | Omuz genişliğinde, dizler hafif bükülü |
| N2 | Fare bırakılmış, normal duruş 2 | Göğüs önünde, sağ omza yakın | Yukarı ve geriye, sol omzun üstüne yaslanmış (taşıma) | Ayaklar dar, dik duruş |
| S1 | Sol tık, sağ yukarı | Bel hizasında, sağda | Yukarı, sağ omzun yanında çapraz geride | Sağ ayak geride, adımlı duruş |
| S2 | Sol tık, orta | Göbek önünde | Dik yukarı, yüzün önünde | Ayaklar açık, dizler bükülü |
| S3–S4 | Sol tık, sağ omuz | Sağ omuz önünde | Dik yukarı, sağ omuz hizasında (ochs/vom Tag benzeri) | Gövde hafif sola dönük |
| S5 | Sol tık, sağ geniş | Sağda, gövdeden uzakta | Dik yukarı, gövdenin dışında | Gövde sağa açık |
| S6 | Sol tık, sağ öne | Sağda, göğüs hizasında | Yukarı-öne, ~60° çapraz | Gövde sağa dönük |
| S7 | Sol tık, sağ asılı | Sağa uzanmış, göğüs-omuz hizasında | Aşağı sarkık (asılı savuşturma) | Ağırlık sağda |
| S8 | Sol tık, sol alçak | Sol kalçada | Gövdenin önünden sağa yatay (sol alçak hazırlık) | Gövde sola dönük |
| S9 | Sol tık, sol omuz | Sol omuz önünde (sağ kol göğüsten karşıya geçmiş) | Yukarı-sola çapraz, sol omzun yanında | Gövde sola dönük |
| S10 | Sol tık, sol geri | Gövdenin solunda | Yukarı-sola ve geriye | Gövde sola dönük |
| S11 | Sol tık, sol öne | Sol önde | Öne-sola ~45° aşağı | Sağ omuz öne gelmiş |
| S12 | Sol tık, uzanmış | Kollar neredeyse düz, sol-önde, göğüs hizasında | Öne yatay (saplama/uzanma) | Gövde kollarla birlikte öne |
| S13 | Sol tık, uzanmış sol | Kollar düz, öne-sola | Öne-sola, hafif aşağı | Sağ omuz önde |
| S14 | Sol tık, baş üstü | Başın ~20 cm üstünde, kollar neredeyse düz | Dimdik yukarı, ağız öne | Ayaklar açık |
| S15 | Sol tık, sağ alçak | Sağ kalça önünde, alçakta | Sağ-öne aşağı, ucu yere doğru | Sol ayak önde, sağ ayak geride |

Ek kareler (sol tık, arkadan):

| # | Durum | Eller | Bıçak | Gövde |
|---|---|---|---|---|
| S16 | Sol omuz üstü hazırlık | İki el sol omuz önünde, baş hizasında, sağ dirsek dışa açık | Yukarı-sola çapraz ~50°, uç sol arkaya (sol vom Tag) | Gövde sola dönük |
| S17 | Baş üstü yatay | İki el başın sağ üstünde, sağ dirsek yukarı | Başın üstünde yatay, uç sola (öküz / ochs benzeri) | Dik |
| S18 | Sağ omuz üstü hazırlık | İki el sağ omuz önünde, baş hizasında | Yukarı-sağa çapraz ~60° | Gövde sağa dönük, sol omuz öne |
| S19 | Sağ yüksek, bıçak geride | Eller sağ omuzun üstünde, kollar yukarıda | Başın arkasına doğru sola yatık, uç sol arkada | Gövde sağa dönük |

Bu kareler hazırlığın derinliğini gösteriyor:
- Eller omuz üstünde, baş hizasına kadar çıkıyor.
- Bıçak başın üstünden ve arkasından karşı tarafa geçebiliyor (S17, S19).
- Kollar dirsekten büyük açıyla bükülüp yukarı kalkıyor. Dirsek omuz hizasının üstüne çıkabiliyor.

Çıkarımlar:
- **El yolu:** Eller gövdenin önünde, ~±50 cm genişliğinde ve kalçadan başın üstüne kadar uzanan geniş bir küre yüzeyinde dolaşıyor.
- **Ellerin uzaklığı:** Kollar çoğu karede uzanmış, eller göğüsten 40–60 cm uzakta. Bizdeki iki el pivotu göğüs ortasında ve 32–50 cm.
- **Bıçak yönü:** Elin konumuna sıkı bağlı değil. Aynı el konumunda bıçak dik, asılı ya da öne olabiliyor.
  - Sağda: dik (S3–S5) ya da asılı (S7).
  - Solda: dik (S9) ya da öne-aşağı (S11).
  - Bizde bıçak el konumundan türetilen tek bir yöne bağlı. Bu, "silah ucu dönüyor" ve "sağa dönmüyor" sorunlarının kaynağı.
- **Ağız:** Dik pozlarda (S2, S14) ağız hep öne bakıyor, yassı yüz yanlara. Dikey vuruşlarda ağız vuruş yönünde. Bizde bıçağın kendi ekseni etrafındaki dönüşü elin bileğine sabit, bu yüzden dikey vuruşta yassı yüz öne geliyor.
- **Gövde ve ayaklar:** Kılıç sağdayken gövde sağa açılıyor ve sağ ayak geride. Solda tersi.
- **Normal duruşta uç rakibe bakmıyor:** N1'de aşağı-çapraz, N2'de omuza yaslı. Bizde taşıma yönü öne ve 30° aşağı, uç rakibe dönük. Bu doğrudan kullanıcının "uç asla karşıya bakmasın" isteği.

## 2. Referans pozlar: yarım kılıç (half-swording)

| # | Durum | Eller | Bıçak |
|---|---|---|---|
| H1 | Fare bırakılmış 1 | Sağ el kabzada sağ kalçada, sol el bıçağın ortasında sol kalçada, kollar gevşek aşağıda | Kalça hizasında yatay, uç sola |
| H2 | Fare bırakılmış 2 | Aynı tutuş, göbek-göğüs hizasına kalkmış, dirsekler bükülü | Göbek hizasında yatay |
| H3 | Sol tık, sağ dik | Kabza eli sağda alçakta, bıçak eli yukarıda | Neredeyse dik, uç yukarı-öne sağda, topuz aşağıda |
| H4 | Sol tık, çapraz aşağı | Kabza eli göğüste, bıçak eli aşağı-solda | Çapraz aşağı-sola |
| H5 | Sol tık, göğüs | Göğüs hizasında, iki el omuz genişliğinde | Yatay, sağdan sola göğüsün önünde |
| H6 | Sol tık, baş üstü | Kollar düz yukarıda, eller omuz genişliğinde | Başın üstünde yatay |
| H7 | Sol tık, alçak adımlı | Kalça altında | Alçakta yatay, adımlı duruş |

Çıkarımlar:
- Yarım kılıçta iki el arası omuz genişliğinde (~40–50 cm). Bıçak gövdenin önünde, eller arasında bir çubuk gibi duruyor.
- Bizim yarım kılıç kodu bıçağı göğüs önünde "kapı gibi" döndürüyor (`ComputeHalfSwordDir`). Fare bırakılmış duruş (H1/H2, alçak ve yatay) ile baş üstü yatay duruş (H6) yok.
- Kullanıcının istediği kontrol:
  - Yarım kılıca sağ tık tek tıkla girilip çıkılacak.
  - Savuruş yine sol tık basılıyken yapılacak.

## 2b. Referans pozlar: uzun silahlar (mızrak, teber, sopa)

| # | Durum | Eller | Sap |
|---|---|---|---|
| P1 | Fare bırakılmış, arkadan | Sağ el aşağıda sap ucunda (kalça), sol el göğüs önünde yukarıda | Neredeyse dik, baş yukarıda, gövdenin sağ önünde |
| P2 | Fare bırakılmış, önden (kısa sap) | İki el bel hizasında, ~40 cm aralı | Yatay, gövdenin önünde, baş sağda |
| P3 | Fare bırakılmış, yürürken | Arka el kalçada sap ucunda, ön el göbekte | Öne-sağa ~15° yukarı, baş önde (taşıma) |
| L1 | Sol tık, dik | Alt el kalçada, üst el göğüs-omuz | Dik, gövdenin önünde |
| L2 | Sol tık, sağ omuz üstü | Alt el göğüste, üst el sağ omuz üstünde | Baş yukarı-sağda, ~60° |
| L3 | Sol tık, yan dik | Alt el sağ kalçada, üst el baş hizasında öne uzanmış | Baş yukarı-öne ~70° |
| L4 | Sol tık, baş üstü arkaya | İki el başın üstünde, kollar uzanmış | Baş geride-yukarıda, alt uç önde (tepeden vuruş hazırlığı) |
| L5 | Sol tık, sağ-arka çapraz | Alt el göğüste, üst el sağ omuzda | Baş sağ arkada ve yukarıda, gövde sola dönük |
| L6 | Sol tık, sol-arka çapraz | Arkadan: iki el sağ omuz önünde | Sap sol omuz üstünden yukarı-geriye |
| L7 | Sol tık, sağ alçak arka | Alt el bel önünde, sap gövdenin sağından aşağı-arkaya | Baş sağ arkada, alçakta |
| L8 | Sol tık, yere | Gövde öne eğik, kollar aşağı uzanmış, eller ~50 cm aralı | Baş yerde, önde (vuruş sonu) |

Çıkarımlar:
- **Eller arası:** 40–60 cm. Alt el hemen hep sap ucuna yakın, üst el ortaya doğru.
  - Bizdeki mızrak ve teber pozu (`ComputePolearmPose`) iki eli orta noktaya göre ayarlıyor. Eller daha dar ve sap kısa görünüyor.
- **Dinlenme:** Fare bırakılınca sap dik ya da hafif öne yukarı duruyor, baş yukarıda (P1/P3). Bizde baş öne dönük ve ~8° aşağı (`AedirPolearmGuardPitch`).
- **Hazırlık çok derin:**
  - Baş omzun ya da başın arkasına kadar gidiyor (L4–L7).
  - Gövde ters yöne dönüyor, ağırlık arka ayakta.
  - Vuruş sonunda baş yere iniyor ve gövde öne eğiliyor (L8). Bizde öne eğilme yok.
- **Ağız:** Teberde ağız vuruş yönünde. Bizde balta ve teber ağzının ekseni sap dönüşüne sabit.
- **Ayaklar:** Kılıçta olduğu gibi silahın tarafına göre adım alınıyor. Hazırlıkta karşı ayak önde.

## 2c. Referans pozlar: saplama (Alt)

| # | Silah | Eller | Bıçak/sap | Gövde/ayak |
|---|---|---|---|---|
| T1 | Teber, hazır (arkadan) | Alt el kalçada, üst el göğüs önünde | Neredeyse dik, baş yukarıda | Sağ ayak geride, adımlı |
| T2 | Balta, hazır | İki el sağ kalça önünde, sap kısa tutulmuş | Baş sol-öne çapraz yukarı | Sol ayak önde |
| T3 | Teber, saplama | İki el göğüs önünde, kollar tam uzanmış, eller ~25 cm aralı | Yatay öne, ~10° yukarı | Derin hamle: ön diz bükük, arka bacak düz, gövde öne eğik |
| T4–T6 | Kılıç, hazır (yakın) | İki el göbek önünde | Dik ya da hafif çapraz yukarı | Ayaklar açık, dizler bükülü |
| T7–T8 | Kılıç, saplama | Kollar tam uzanmış, eller göğüs-omuz hizasında | Yatay, tam öne, kol çizgisinin devamı | Ön ayak öne, gövde kollarla birlikte öne, arka topuk kalkık |
| T9 | Kılıç, yukarıdan aşağı saplama (üstten) | Kollar başın önünde yukarı uzanmış | Öne-aşağı, uç rakibe | Gövde öne eğik |

Çıkarımlar:
- **Hizalama:** Saplamada bıçak (ya da sap) ön kolların tam devamı. Bilek düz, eller kol çizgisinde.
  - Bizim saplama modunda (`bThrustMode`) yön kameranın eğimine bağlı. Bilek hizası `ApplyHandAim` ile ayrıca yapılıyor ve yamuk kalabiliyor.
- **Hamle:** Saplama tüm vücutla yapılıyor. Ön ayak öne atılıyor, gövde 15–25° öne eğiliyor, arka topuk kalkıyor.
  - Bizde saplama yalnızca kolu uzatıyor (`Extension`). Hamle adımı ve gövde eğilmesi yok.
- **Mızrak ve teber:** Eller daralıyor (~25 cm), iki el de sapın arka yarısında.
- **Üstten saplama:** Kollar yukarıdayken (T9) uç aşağı-öne. Kamera eğimiyle yönlendirme bunu sağlıyor; korunmalı.

## 3. Aedir hata kareleri

| # | Görülen | Kök neden |
|---|---|---|
| A1 | Normal duruşta uzun kılıç göğüs önünde, uç yukarı-öne, kollar göğüste çapraz | Taşıma yönü öne ve 30° aşağı sabit. İki elli silah fight stance dışında da iki elle göğüs önünde tutuluyor. Referanstaki N1 alçak ve aşağı-çapraz |
| A2 | Baş üstünde eller kabzanın iki ucunda açık, kılıç başın üstünde düz | Baş üstü pozda kabzaya kalibre tutuş doğru. Ama bıçak yatıyor, referanstaki S14'te dik |
| A3 | Kılıç ters: uç aşağı-öne, topuz yukarı; eller kabzada ama bilekler bükük | Bıçak hedef yönü el konumundan türetiliyor. Alçak-öne konumda yön aşağı dönüyor ve bilek kırılması tersine çalışıyor |
| A4 | Kılıç sağa yatay, kollar bükülmüş | Sağa dışa dönüş, el alçakken 55° ile sınırlı (son turdaki titreme düzeltmesi). Bıçak hedefi gövdeden temizlenince yatıyor |
| A5 | Bıçak sol arkada, kol garip bükülü | Karşıya hazırlıkta bıçak omzun üstünden arkaya kırılmıyor (son turda kapatıldı). Kol karşıya geçerken dirsek ters dönüyor |
| A6 | Yürürken kılıç sağa yatay, uç öne-sağa. Ekranın sağ yarısı siyah | Taşıma yönü (öne-yana). Siyah alan FSR hatası ya da üst ölçekleyici ile AA çakışması (bkz. bölüm 9) |

## 4. Silah savurma aralığı: yapılacak model

Kullanıcı:
- "Soldan arkaya kadar çekmiyor, sağa dönmüyor bile."
- "Yukarı kaldırırken uç dönüyor, aşağı yeterince inmiyor."
- "Dikey darbelerde keskin tarafı kullanmıyor, kol saçma şekillere giriyor."

Bizdeki model (`ComputeHandLedHandLocal` / `ComputeHandLedBladeDir`):
- Tek el omuz pivotu çevresinde, iki el göğüs ortası çevresinde dönüyor.
- Dönüş sınırları: dışa 105°, içe 80° (iki elde 85° ve 50°, alçakta 55° ve 30°).
- Yükseklik −50° ile +82° arasında.
- Bıçak = kol çizgisinin 50° üstü, bilek kırılmasıyla karıştırılıyor.

Neden yetersiz:
1. **Sınırlar:** Titremeyi bastırmak için kısıldı (alçakta 55°, iki elde karşıya 50°). Kök neden (bıçağın kendi bedene girmesi ve fizik kolların gövdeye sürtünmesi) çözülmeden genişletmek mümkün değil.
2. **Bıçak yönü:** El konumunun bir fonksiyonu. Referansta bıçak yönü bağımsız bir serbestlik (S3 dik / S7 asılı). Half Sword'da büyük ihtimalle fare iki şeyi sürüyor (?): elin yolunu ve bıçağın ağırlıklı ataletini. Bıçak, elin hareket yönünün tersine sallanıyor.
3. **Ağız hizası:** Yok. Bıçak kendi ekseninde, bileğin o anki dönüşüyle duruyor.
4. **Alt sınır:** −50° ve uç yerin 12 cm üstünde tutuluyor. Referans S15'te uç yere kadar iniyor.

Uygulama planı (görsel doğrulamayla):
- (a) **El yolu:** Pivot hem tek hem iki elde omuz-göğüs arası. Erişim 35–60 cm. Sağ ve sol uçlarda gövde dönüşü 60°'ye kadar ve ayaklar ona göre adım alacak (bkz. 7).
- (b) **Bıçak yönü:**
  - Her referans pozu bir anahtar poz olacak: (el konumu, bıçak yönü, bıçak dönüşü, gövde dönüşü, ön ayak).
  - Fare konumu anahtar pozlar arasında yumuşak geçişle (2B ızgarada) karıştırılacak.
  - Bıçak, bu hedef etrafında ağırlığıyla gecikecek.
  - Böylece "uç dönüyor" ve "saçma kol" durumları anahtar pozlarla sınırlanır, çünkü her anahtar poz görsel olarak doğrulanmış olur.
- (c) **Ağız hizası:** Hızlı savuruşta bıçak kendi ekseni etrafında, ağız hareket yönüne bakacak şekilde dönecek (yumuşak hizalama). Durağanken anahtar pozun dönüşü geçerli olacak.
- (d) **Doğrulama:** Her anahtar poz için önden, yandan ve arkadan kare çekilip referansla yan yana konacak. Titreme sondası (`JitterProbe`) her pozda geçmeli.

## 5. Darbe tepkisi, denge, düşme ve kalkma

Ölçüm (`fin1`, 3 NPC dövüşü):
- Dövüş başına 6–9 düşüş oldu.
- Vuruşların yarısı yerdeki birine geldi.
- 75 J'lük bir köprücük kemiği vuruşu dengeyi 0,68'den 0,37'ye, 84 J'lük boyun vuruşu sıfıra indirdi.

Kök nedenler:
1. **Denge kaybı çok yüksek.** Son turda bölge referansları gövde için 140 J'e, dengeye giden pay %60'a ayarlandı. Tek bir orta darbe dengeyi bitiriyor.
2. **Denge yere düşmeyle eşleşiyor.** Denge 1 sn boyunca 0,25'in altında kalınca `EnterRagdoll`: motorlar ragdoll gücüne iniyor ve beden "kütük gibi" düşüyor. Arada adım atarak toparlanma yok.
   - `BeginRecoverySwing` adımı var ama denge kaybıyla sürekli bağlı değil.
   - Devrilme yönü yalnızca "knockdown" itişinde veriliyor.
3. **Knockdown.** Enerji eşiği aşılınca anında ragdoll ve devirme hızı veriliyor; "sıçrayıp düşme" buradan geliyor.
4. **Kalkma.** Animasyon pozu boyunca eklem motorları ile sürülüyor. Elin yere dayanması, ağırlığın dizden geçmesi gibi fiziksel destek yok (bkz. 5b).
5. **Hasar.** Ortalama vuruş 13–23. Boyun ve baş 44–65 alıyor. Sınır gövdede %40, başta %65. İki-üç vuruşta ölüm, kanamayla daha hızlı.

Uygulama planı (araştırma bulgularıyla birlikte, bkz. 5a):
- **Denge:** Kaybı yeniden yumuşatılacak; tek orta vuruş en çok ~0,3 almalı.
- **Toparlanma:** Denge kaybının yönüne doğru basınç merkezi kayınca adım (`BeginRecoverySwing`) sürekli denenecek.
  - Her adım dengenin bir kısmını geri verecek.
  - Ancak 2–3 adımda toparlanamazsa düşme başlayacak.
- **Düşme:** Doğrudan ragdoll yerine kontrollü olacak.
  - Motorlar %30–50'de kalacak.
  - Dizler bükülecek, eller düşme yönüne uzanacak (koruyucu refleks zaten var).
  - Gövde düşme yönüne devrilecek.
- **Knockdown:** Yalnızca çok büyük darbelerde.
- **Hasar:**
  - Tek darbe sınırı gövdede %25, başta %40 olacak.
  - Kanamanın ağırlığı artacak, ölüm zamana yayılacak.
  - Hedef: zırhsız ölüm için 3–6 temiz kesik, bilinç kaybı ve sakatlık ara durumlar.

## 6. Çarpışma: kapsül ve gövde

Kullanıcı:
- "Silahlar görünmez bir kapsüle takılıyor" dedim, sen silahları gövdeden geçirdin.
- Silah bedene çarpmalı; bazen kesip içinden geçmeli, bazen geçememeli.
- Beden her şeye tepki vermeli, koşup duvara da çarpabilmeli.

Koddan bulgular:
- **Kapsüller birbirini engelliyor.** Pawn–Pawn "Block", varsayılan karakter kapsülü yarıçapı ~34–42 cm. Bu yüzden iki karakterin merkezleri ~70–85 cm'den yakına gelemiyor; bedenler hiç temas etmiyor. Asıl "görünmez kapsül" bu.
- **Fizik gövdesi.** Mesh, fizik gövdesi (PhysicsBody) olarak her şeyi engelliyor, kapsülleri (Pawn) yok sayıyor.
- **Elde tutulan silah:**
  - Son turda PhysicsBody'yi yok sayacak şekilde değiştirildi; bedenlere fiziksel olarak çarpmıyor.
  - Hasarı sanal tarama veriyor; "dokunuş" darbelerinde kolu durduruyor.
  - Görsel olarak bıçak bedene girebiliyor; vuruş hissi bu yüzden kayboldu.

Kullanıcının önerisi: kapsülü ayak tabanında küçük bir nokta yapmak.
- **Uygulanabilir mi:** Kısmen. UE'nin `CharacterMovementComponent`'i kapsül gerektiriyor.
- **Ama:**
  - Yarıçap ~12–15 cm'ye indirilebilir, yükseklik de ayak bölgesine yakın tutulabilir.
  - Kapsüller birbirini yok sayabilir (Pawn–Pawn Ignore).
  - Bedenlerin birbirine ve duvara teması fizik gövdeleriyle olur.
- **Gerekenler:**
  - (1) Kapsülün diğer kapsülleri yok sayması.
  - (2) Fizik gövdesinin itildiği yönde kapsüle de geri besleme (pelvis kapsülden saparsa kapsülü o yöne kaydırmak). Yoksa iki karakter birbirinin içinden yürür.
  - (3) Elde tutulan silahın diğer bedenlerin fizik gövdelerine yeniden çarpması.
  - (4) Sanal vuruşun kesme payına göre silahı bedenin içine biraz sokması ya da durdurması. Mevcut "glance / dragged / went through" sonuçları bunu belirliyor; fizik çarpışması "went through" iken kesme süresince kapatılmalı.
- **Önceki "görünmez engel" düzeltmesi:**
  - Vuruş yarıçapını 7 cm'den bıçak kalınlığına indirmek doğruydu, kalmalı.
  - Fiziği tamamen kapatmak yanlıştı, geri alınmalı.

## 6b. Half Sword araştırma bulguları

Etiketler:
- **[G]** geliştirici yorumu ya da yama notu
- **[T]** topluluk
- **(?)** bilinmiyor

### Gövde ve çarpışma [G]

Geliştiricinin itch.io yanıtlarına göre:
- **Kapsül bağı:** Fizik gövdesinin pelvisi, normal bir hareket kapsülüne serbest limitli, motorlu bir kısıtla bağlı. Motorlar gövdeyi "yerinde ve dik" tutuyor; motor güçleri doğal görünsün diye sürekli ayarlanıyor.
- **Kapsül gövdeyi izliyor:** Kapsül ile mesh "lastikle bağlı". Kapsül sürekli mesh'e doğru çekiliyor, yani **gövde önde, kapsül arkadan geliyor**.
- **Hedef iskelet:** Gizli, simüle edilmeyen bir iskelet Animation Blueprint'i çalıştırıyor. Fizik mesh eklem motorlarıyla onu kopyalıyor (Twist/Swing).
- **Animasyon:** Uzuvların tamamı prosedürel ve iki kemikli IK ile sürülüyor. Adımdan önce zemine ışın atılıyor.

Erken Erişim yama notları:
- "Karakter ile kendi silahı arasındaki çarpışma kapatıldı." Silahlar ve vücut parçaları birbirine çarpan fizik nesneleri; yalnızca kişinin **kendi** silahıyla çarpışması kapalı.
- 0.6.1'de aktif ragdoll baştan yazıldı ve bilekler sertleştirildi.

"Hitbox yok" iddiası yalnızca üçüncü taraf bir rehberde geçiyor. Kesip geçmenin nasıl yapıldığı açıklanmamış (?).

**Bizim için sonuç:**
- **Kapsül:** Kalacak ama küçük olacak, diğer kapsülleri yok sayacak. Pelvis bir yöne itildiğinde kapsül de o yöne kayacak (gövdeyi izleyen kapsül).
- **Çarpışma:** Elde tutulan silah ile vücut parçaları diğer bedenlere fiziksel olarak çarpacak. Kendi bedeniyle çarpışması kapalı kalacak.
- **Kesme:** Kesme sırasında bıçak için kısa bir süre geçiş izni verilecek (bkz. bölüm 6).

### Denge ve düşme

- [G] Mayıs 2025: "Düşmek artık daha zor, kalkmak daha hızlı."
- [G] Ağır zırh ve silah hareketi ağırlaştırıyor; güç bu cezayı azaltıyor.
- [G] Yerdeyken dövüş mümkün: elleri tam kontrol, tekme, yerden tutup çekme.
- [T] Baş darbesi genelde düşürüyor; kol ve baldıra hafif vuruş da düşürebiliyor. Shift (dövüş duruşu) tökezlemeyi azaltıyor.
- [T] Düşen genelde kaybediyor. Oyuncular bir darbenin "kasları durdurduğunu" söylüyor.
- [T] **Kalkma fizikle sürülüyor, hazır animasyon değil:**
  - Sırtüstüyken W, yüzüstüyken S basılı tutuluyor.
  - Fare, ellerin ittiği yönü veriyor: bacaklara doğru itmek kaldırıyor, gövdeye doğru itmek sürünmeye geçiriyor.
  - Mızrakla diz çökmüş hâlden itilerek kalkılabiliyor.
  - Zırh ve üstteki düşman kalkmayı yavaşlatıyor.

### Hasar [G]

- Hayati organlara daha çok, diğer bölgelere daha az hasar gidiyor. Karaciğer vuruşu ayrı.
- Bilinç ile ölüm ayrı: boğma önce bayıltıyor, sonra öldürüyor.
- Kemik kırılması ve çıkık var. Acı yapay zekâyı teslim ettiriyor; kemik kırmak dövüşü bitiriyor.
- 0.6.1.5'te künt, çıplak yumruk ve sap hasarı azaltıldı. İkiye bölünen biri bile hemen ölmüyor.
- Kesik başına hasar sayıları yayımlanmamış (?).
- Bıçakla kesmek gambesona karşı zayıf, plakaya karşı neredeyse işe yaramıyor; saplama güvenilir [T].

**Bizim için sonuç:**
- Ölüm çoğunlukla kanama ve hayati organdan gelsin; tek kesik nadiren öldürsün.
- Acı, kırık ve bilinç kaybı dövüşü bitiren ara durumlar olsun.

### Kontroller (Erken Erişim varsayılanları) [G]/[T]

| Eylem | Tuş |
|---|---|
| Sağ el / sol el savuruş | LMB / RMB basılı + fare |
| Tutma | E/Q dokun |
| Kına koyma | E/Q basılı tut |
| El değiştirme | X |
| Silahı çevirme / mordhau (yarım kılıçtan) | X basılı tut |
| Saplama | Çift tık + basılı. Alt'a dokun: klasik saplama, Alt basılı: başparmak tutuşu |
| Yarım kılıç | Yeni varsayılan RMB + LMB birlikte. "Klasik" seçenekte RMB'ye tıklayıp aç/kapa |

Kullanıcının istediği düzen, Half Sword'un klasik yarım kılıç düzenine çok yakın.

### Silah hareket aralığı

- [G] 0.6.1'de prosedürel animasyonlar "daha fazla açıdan kesilebilsin" diye yeniden yazıldı. Bilekler sertleştirildi.
- [T] Fare hareketi **tuşa basılan noktaya göre** okunuyor: uzağa sürüklemek kolu gerer, tuşu bırakıp yeniden basmak nötre döndürür. Kamera eğimi savuruş açısını değiştiriyor. Yana adım atarken savurmak momentum ekliyor.
- Hazırlık açısı sınırları, ağız hizalama yardımı ve dinlenme duruşu yayımlanmamış (?). Bu yüzden kullanıcının referans kareleri (bölüm 1, 2 ve 2b) birincil kaynak.

### Grafik

- Half Sword çıkışta DLSS 4 Multi Frame Generation ve Reflex ile geldi [NVIDIA]. DX11 ve DX12 arasında geçiş yapılabiliyor.

## 7. Diğer maddeler

- **El değiştirme / bele koyma / belden alma:** El gitmesi gereken yere gitmiyor. `HolsterTest` ve belden çekmenin kare dizisi çekilip el hedefi ile gerçek el arası ölçülecek (`GaitInfo handToItem`).
- **Koşarken bilek dışa dönüyor:** Son turda taşıma yönü ön kola yaslandı. Kullanıcı hâlâ görüyor; muhtemelen iki elli silah koşarken iki elle tutulduğunda ya da `ApplyHandAim` bilek sınırında.
- **Normal duruşta uç karşıya bakmasın:** Taşıma yönü N1 gibi aşağı-çapraz (tek elde aşağı-öne ~60°, iki elde kasık önü çapraz) olacak. N2 (omuza yaslı) ikinci bir dinlenme pozu.
- **İlk ele alırken kolların çıldırması:** Test yerden alma başarısız oldu (karakter eğildi ve eli boş döndü). Ayrıca incelenecek: oturma (settle) karışımı, iki el kavrama karışımı ve ilk karedeki `WeaponDir` sıfırlaması.
- **Tuşlar:**
  - Bele koy: Q/E basılı tutarak. Kullan: T/Y. Bugün T/Y bele koyuyor, Q/E alıyor ve kullanıyor.
  - Yarım kılıç: sağ tık tek tıkla geçiş. Sağ tık basılı: sol el ayrı.
  - Hançer, küçük bıçak ve aletler dışındaki silahlar, öteki el boş ve sağ tık basılı değilse iki elle tutulacak.
- **Kontroller bugün** (`AedirPlayerCharacter.cpp:317-340`):
  - LMB/RMB sağ/sol koruma; RMB basılıyken iki elli tutuşta yarım kılıç (`bTwoHandGrip && bLeftGuard`).
  - Q/E el (alma ve kullanma), T/Y bele koyma, X el değiştirme, Alt saplama, R silahı çevirme, C tekme.
  - Yeni düzen için:
    - RMB'nin tıklama ve basılı tutma olarak ayrılması (tıklama < ~0,25 sn: yarım kılıç aç/kapa; basılı: sol el ayrı).
    - Q/E'nin basılı tutma süresiyle ayrılması (kısa: al; uzun: bele koy / belden al).
    - T/Y'nin kullanıma bağlanması.
    - İki elle tutuşun varsayılan olması (silah sınıfına bakılarak; hançer ve aletler hariç).
- **Fazla savrulma:** Darbe sonrası beden ve kollar fazla salınıyor. Gevşeme (flinch), itme ve gövde dönüşü azaltılacak, Half Sword görselleriyle karşılaştırılacak.

## 7b. Kullanıcının ek gözlemleri (10 Kasım 2026) ve kod durumu

### Merkezden dışa iki bölgeli savuruş

Kullanıcının gözlemi (referans karelerle uyumlu): fare merkeze yakın bir halkadayken silahın ucu rakibe doğrultuluyor. Belli bir mesafeden sonra silah dışa çevriliyor ve arkaya uzanıyor.

**Model:** fare ofseti, kutupsal koordinatlarda (yarıçap r, açı θ):
- **İç bölge** (r küçük, ~ilk %35–40): Eller gövdenin önünde, θ yönüne biraz kayıyor. Bıçak rakibe dönük, ucu θ yönünde hafifçe sapıyor. Bu bölge korumalar, saplama hazırlığı ve küçük düzeltmeler için.
- **Geçiş halkası:** Uç dışa dönmeye başlıyor. Dirsek açılıyor, gövde θ yönüne dönüyor.
- **Dış bölge** (r büyük): Eller θ yönünde omuz ya da baş hizasına çıkıyor. Bıçak dışa ve arkaya uzanıyor, uç θ'nın karşısına bakıyor. Bunlar hazırlık pozları (S3–S7, S9, S16–S19). Gövde 45–60° dönüyor, ayak adımı da bu bölgede alınıyor.

Anahtar poz ızgarası buna göre kurulacak:
- Her θ için bir iç poz ve bir dış poz olacak.
- r boyunca iç pozdan dış poza geçilecek. Geçiş, bıçağın ucunun bir yay çizerek (rakip → yan → arka) dönmesiyle olacak.
- Bıçak hiçbir anda başın içinden geçmeyecek; yol, poz tanımının parçası olacak.

### Kan

Gözlem: Kan sabit bir görsel değil. Vücuttan ve silahtan partikül olarak sıçrıyor, düştüğü yeri kırmızıya boyuyor.

Bugünkü durum (`AedirBloodSubsystem`):
- `AAedirBloodDrop` damlaları var (en çok 64). Düştükleri yere decal bırakıyorlar (en çok 700, 3 oyun günü ömür).
- `SpawnSpray` de var.

Yapılacak:
- Darbe anında yaradan **ve bıçaktan** (bıçağın hareket yönünde savrulan) çok sayıda küçük Niagara partikülü saçılacak.
- Her partikül çarpınca o yüzeye küçük bir decal bırakacak. Bu decal'ler zeminde, bedende ve silahta görünecek.
- Damla sayısı sınırı GPU partikülüne taşınacak; boyama yalnızca çarpan bir kısım partikülden olacak.
- Bıçağa da kan izi bırakılacak (malzeme parametresiyle).

### Ölünün silahı

Gözlem: Half Sword'da ölünün elindeki silaha basmak hasar vermiyor; hasar için tutanın canlı olması gerekiyor. Bizde bu daha kusurlu çalışıyor.

Kod:
- `UpdateImpacts` tutanın ölü olup olmadığına bakmıyor.
- Son turda "tuş basılı olmasa da hızlı giden silah vurur" kuralı eklendi. Bu yüzden ölen birinin savrulan kolundaki ya da düşen silahı can alabiliyor.

Yapılacak:
- Tutan ölü, bilinçsiz ya da ragdoll'daysa silah hasar vermeyecek; yalnızca fiziksel itme kalacak.
- Yerdeki gevşek silah da hasar vermeyecek (bugün de vermiyor).

### Ceset çürümesi

Zaman çizelgesi:

| Oyun günü | Ceset |
|---|---|
| 1. gün sonu | Ten solmaya başlıyor (gri-sarımsı) |
| 2.–7. gün | Soluk kalıyor |
| 7. günden sonra | Koyulaşıyor, koyu kahverengiye dönüyor |
| 30. günden sonra | Aynı yerde, aynı duruşta iskelete dönüşüyor |

Bugün böyle bir sistem yok (`corpse` ya da `decay` araması boş).

Yapılacak:
- Cesette ölüm zamanı tutulacak (`UAedirWorldClock` / oyun günü).
- Ten ve giysi malzemelerine bir "çürüme" parametresi eklenecek: renk ve doku karışımı.
- 30. günde iskelet: iskelet mesh'i, ceset pozunun kemik dönüşümleri kopyalanarak aynı yerde kurulacak (statik poz).
- Ceset kaydı kalıcı olacak; kayıt dosyasında tutulacak.

### Kamera sarsıntısı

Gözlem: Oyuncu darbe aldığında çok hafif bir kamera sarsıntısı olmalı.

Bugün `ApplyHitReaction` kontrol yönünü darbe yönünde 6°'ye kadar itiyor (`HitReactionViewDegreesPerJoule`), ama sarsıntı yok.

Yapılacak:
- `UCameraShakeBase` (Perlin / dalga), 0,15–0,3 sn, genlik enerjiyle ölçeklenecek ve küçük tutulacak.
- Kamera itişi 6°'den 2–3°'ye inecek.
- Ayarlar menüsüne "Kamera sarsıntısı" kaydırıcısı.

### Kamp ateşi

Gözlem: Ses çok yüksek, ateş ateşe benzemiyor ve çok parlak, altında ateş alanı yok.

Kod (`AedirSceneryActors.cpp`):
- Ses `Amb_Fire_Burn_Large_Dist_LP_01`, büyük ve uzak bir yangın döngüsü. Duyulma mesafeleri: 150 cm tam ses, 1800 cm sönüm, en düşük ses 0,06.
- Işık nokta ışığı, 1600 cm. Alev, birkaç parlak "dil".

Yapılacak:
- **Ses:** Küçük kamp ateşi çıtırtısı (`_Content/SFX/Fire` içinden küçük bir döngü). Ses 0,35'e inecek, sönüm 700 cm olacak.
- **Ateş:** Sahnede gerçekçi bir ateş Niagara sistemi varsa o, yoksa alevler kaldırılacak. Altına odun ve taş halkası mesh'i, kömür ve kül decal'i eklenecek.
- **Işık:** Yoğunluk ~1/3'e inecek. Hafif titreme olacak (zaten var mı bakılacak).
- Uygun ateş efekti bulunamazsa ateş tamamen kaldırılacak.

### Ses efektleri

Bugün yalnızca ayak sesleri ve hava ve sahne sesleri var. Dövüşte hiç ses yok: `PlaySound` yalnızca footstep ve weather tarafında çağrılıyor.

Eklenecek olaylar ve kaynakları:

| Olay | Kod noktası | Ses |
|---|---|---|
| Koşarken bir yere çarpma | `AedirBalanceComponent` duvar / nesne darbesi (`AedirWallImpactMaxLoss`) | Gövde çarpması, yüzeye göre |
| Demir silahın yüzeye çarpması | `ResolveHeldCollision` / `ApplyPhysicsImpact` (yüzeyin fizik malzemesine göre) | Metal çınlaması: taş, tahta, toprak |
| Tahta silahın çarpması | Aynı nokta, sapın ya da gövdenin malzemesine göre | Tahta tok sesi |
| Yumruk | `ApplyCreatureImpact`, eldeki eşya yokken | Et tokadı |
| Darbe alma | `ApplyCreatureImpact` / `ApplyHitReaction` | İnleme ya da nefes, enerjiye göre |
| Kılıcın deriyi kesmesi | `ApplyCreatureImpact` sonucu "dragged / went through / stuck" | Kesik, saplama, et |
| Hızlı savuruş | Kolun ya da ucun hızı eşiği geçince (`UpdateArms`) | Hava yarılması; hızla ses ve perde artıyor |
| Tahta kesme | Eğitim kuklası / tahta yüzey vuruşu (`AAedirTrainingDummy::ReceiveStrike`) | Tahtaya saplanma ve yarılma |
| Silahların çarpışması | `[Clash]` (`ReceiveClash`) | Çelik-çelik çınlaması, hıza göre |
| Kemik kırılması, zırh vuruşu | `BreakLimb`, `TakeArmorBlow` | Çatırtı, plaka sesi |

- Ses dosyaları projedeki `/Game/Audio/_Content/SFX` altından seçilecek. Önce klasör taranıp her olaya uygun dosya listelenecek. Eksik olanlar için kullanıcıya bilgi verilecek.
- Her olay bir `USoundBase` + `USoundAttenuation` ile çalınacak. Ses perdesi ve hacmi darbe enerjisi ya da hızla ölçeklenecek, kısa bir tekrar sınırı olacak.
- Doğrulama: Her olayın logda "[Sfx] olay → dosya" satırıyla çıkması ve gerçek bir dövüş kaydında her olayın en az bir kez duyulması.

## 8. Çimen (kod ve log incelemesi)

**En büyük sorun: çimen oyunda yanlış malzemeyle çiziliyor.**
- Megascans çimen malzemelerinde `Used with Nanite` bayrağı yok: `M_WildGrass`, `M_MeadowGrass`, `M_GrassClumps`.
- Log, 104 oturumun hepsinde şu uyarıyı veriyor:
  `Material ... M_WildGrass missing usage flag Nanite! Default Material will be used in game.`
- Sonuç: 45.752 çimen kümesinin hepsi motorun varsayılan opak malzemesiyle çiziliyor. Saydamlık maskesi, rüzgâr ve eğilme yok. "Uyduruk" görüntünün asıl nedeni bu.

**Yapı.**
- Çimenler, ağaçlar ve çiçekler düz `UInstancedStaticMeshComponent` (ISM). Her mesh için tek bir bileşen var, toplam 26 bileşen.
- Foliage olmaması Nanite'ı engellemiyor. UE5'te Nanite açık mesh'lerle ISM de Nanite olarak çiziliyor. Engelleyen tek şey malzeme bayrakları.
- PN çiçek mesh'leri ise Nanite değil.

**Gölge yok.**
- Her çimen bileşeninde `SetCastShadow(false)` ayarlı (`AedirFoliageSubsystem.cpp:365, 645`). Bu, temas gölgesini de kapatıyor.
- Güneşin `ContactShadowLength` değeri 0. Menüdeki "Contact shadows" seçeneği güneş için hiçbir şey yapmıyor.

**Performans.**
- **Kırpma mesafesi:** 50–75 m, ama çayır 64 m. Pratikte hiç kırpılmıyor; ~53,6 bin örnek her karede çiziliyor.
- **WPO:** `WPODisableDistance` yok, WPO her mesafede hesaplanıyor.
- **Ezilme sorgusu:** `GetInstancesOverlappingSphere` her ayak için 0,1 sn'de bir tüm örnekleri tek tek dolaşıyor. Her ayak adımında ~53 bin test.
- **Açılış:** Her açılışta ~57 bin ışın taraması ve yeniden dikim yapılıyor.
- **Yoğunluk ayarı:** Menüdeki "Grass density" (`grass.DensityScale`) bu ISM'leri hiç etkilemiyor.

**Plan.**
1. Üç malzemede `bUsedWithNanite` açılacak, `MaxWorldPositionOffsetDisplacement` ~15 cm. Çiçeklerde Nanite açılacak ya da düşük ayarda çiçekler kaldırılacak. Dikim editörde bir kez yapılıp kaydedilecek.
2. Menüye "Çimen gölgesi" seçeneği (`aedir.Grass.Shadows`):

   | Ayar | Ne yapıyor |
   |---|---|
   | Kapalı | Gölge yok |
   | Düşük | Yalnızca temas gölgesi; güneş `ContactShadowLength` ≈ 0,04 |
   | Orta | VSM ile, `ShadowCacheInvalidationBehavior = Rigid`, `WPODisableDistance` 20 m |
   | Yüksek | VSM ile, `Auto`, 30 m |

3. Mesafe (`aedir.Grass.Distance`) ve gerçekten çalışan yoğunluk (`aedir.Grass.Density`) ayarları. Yoğunluk için örnek başına sabit rastgele eşik, ya da `bEnableDensityScaling` ile HISM.
4. Çayır 16 m'lik karolara bölünecek. Gölge ve kırpma karo bazında yapılacak. Ezilme sorgusu için ızgara karması kullanılacak; ezilen örnekler bir haritada tutulacak.

## 9. Üst ölçekleyiciler (upscaler), AA, Frame Generation, Reflex (kod ve log incelemesi)

**FSR hatası.**
- `Plugins/FSR/FSR.uplugin` (4.1.1) ve `FSRMovieRenderPipeline.uplugin` `"EngineVersion": "5.7.0"` diyor.
- Log: `'FSR' is Incompatible ... Attempt to load it anyway? → No → Skipping load of 'FSR'`.
- İkili dosyalar zaten 5.8 için derlenmiş (BuildId motorla aynı). Tek engel bu alan.
- Çözüm: iki dosyada `EngineVersion` değerini `5.8.0` yapmak.
- FSR yüklenince `r.FidelityFX.FI.Enabled` (kare enterpolasyonu) varsayılan olarak 1. FSR kare üretimi seçilmedikçe 0'a zorlanmalı.

**AA çakışması.** İki ayrı sorun var.

1. Öncelik sırası:
   - Ölçekleyici seçimi cvar'ları `SetByGameSetting` önceliğiyle yazıyor.
   - Menüdeki "Anti-aliasing method" satırı `r.AntiAliasingMethod`'u daha yüksek olan `SetByCode` önceliğiyle yazıyor.
   - AA satırı bir kez değişince (ya da açılışta ini'den yüklenince) ölçekleyicinin AA'yı TSR'a çekmesi sessizce yok sayılıyor.
2. TSR'a zorlama eksik:
   - UE5'te DLSS, FSR ve XeSS ancak `r.AntiAliasingMethod = 4` (TSR) ya da TAA + upsampling açıkken devreye giriyor ve TSR geçişinin yerine geçiyor.
   - AA None, FXAA ya da MSAA iken ölçekleyici hiçbir şey yapmıyor; görüntü yalnızca büyütülüyor.
   - DLSS seçimi AA'yı hiç ayarlamıyor.
   - Bu durum ekrandaki bozulma ve siyah alanla tutarlı.
- Ayrıca:
  - DLSS seçeneği yalnızca bir cvar var mı diye bakarak sunuluyor (AMD ve Intel'de de görünür).
  - FSR'da `QualityMode`, XeSS'te `r.XeSS.Quality` ayarlanmıyor.
  - "Render scale" satırı ölçekleyici açıkken etkisiz.

**Frame Generation ve Reflex.**
- Menüde hiç yoklar.
- Bu makinede (RTX 4070 SUPER) log şunları söylüyor:
  - DLSS-SR destekli. DLSS-G destekli, yalnız 2X; donanım GPU zamanlaması gerekiyor.
  - Reflex destekli.
  - XeFG ve XeLL bağımsız oyunda destekli görünüyor.
- DLSS-G ve Reflex editördeki viewport PIE'de çalışmıyor; bağımsız (Standalone) ya da paketli oyunda test edilmeli.

**Menü tasarımı.**

*Ölçekleyici satırı:* Kapalı (TAA), TSR, DLSS, DLAA, FSR, XeSS. Yalnızca desteklenenler gösterilecek:
- DLSS: `UDLSSLibrary::IsDLSSSupported` ve `IsDLSSModeSupported`.
- XeSS: `r.XeSS.Supported`.
- FSR: cvar var ve RHI D3D12.

*AA satırı:*
- Ölçekleyici açıkken kilitli ve "Ölçekleyici yönetiyor" yazacak; `r.AntiAliasingMethod = 4`.
- Yalnızca ölçekleyici kapalıyken açık: None, FXAA ya da TAA (MSAA deferred'da çalışmadığı için gizli).
- Tüm yazımlar tek öncelikle yapılacak (`SetByGameSetting`).

*Kare üretimi satırı* (yeni, ölçekleyiciye bağlı):

| Ölçekleyici | Kare üretimi |
|---|---|
| DLSS / DLAA | DLSS-G (`r.Streamline.DLSSG.Enable` 0/1/2, `FramesToGenerate = 1`) |
| FSR | `r.FidelityFX.FI.Enabled` |
| XeSS | `r.XeFG.Enabled` (yeniden başlatma gerekebilir) |
| Diğerleri | Kilitli |

Diğer üreticilerin kare üretimi her zaman 0 tutulacak, aynı anda yalnızca biri açık olabilir.

*Düşük gecikme satırı* (yeni):
- Reflex Kapalı / Açık / Açık + Boost (`t.Streamline.Reflex.Enable`, `t.Streamline.Reflex.Mode`). XeSS ile XeLL.
- DLSS-G açıkken en az "Açık"ta kilitli; XeFG açıkken XeLL açık kilitli.

*Diğer kurallar:*
- **VSync:** DLSS-G açıkken `GetDLSSGIsVsyncSupportAvailable` false ise kilitli.
- **Duraklatma menüsü:** Açıkken DLSS-G geçici olarak kapalı (ya da `r.Streamline.DLSSG.FullScreenMenuDetection = 1`).
- **Altyapı:**
  - `FAedirSettingOption`'a "etkin / görünür" geri çağrısı eklenecek.
  - `SAedirPauseMenu` satırı `bDimmed` ile soluk çizecek (altyapısı hazır).
  - Kilitli satır girdi almayacak.
  - Ölçekleyici değişince kilitler yeniden okunacak.
- **Build.cs:** `DLSSBlueprint`, `StreamlineDLSSGBlueprint`, `StreamlineReflexBlueprint` ve XeSS/XeFG/XeLL Blueprint modülleri eklenecek.
