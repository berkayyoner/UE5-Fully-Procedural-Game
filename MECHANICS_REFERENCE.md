# Dövüş mekanikleri referansı (Half Sword gözlemleri ve Aedir karşılaştırması)

Kaynak: yalnızca herkese açık bilgiler. Kullanılanlar:
- Steam yama notları (2024–2026)
- Geliştiricinin itch.io yanıtları
- Epic Games Store röportajı
- Rehberler ve wiki
- Resmî fragmanlardan alınan kareler

Hiçbir dosya, kod ya da model açılmadı. Amaç aynı davranışı ve hissi kendi kodumuzla kurmak. Görsel kimlik (karakterler, zırh tasarımları, mekânlar, arayüz) Aedir'e ait kalır.

İşaretler: **[G]** geliştiricinin kendi açıklaması, **[T]** topluluk ya da rehber, **(?)** belirsiz.

## 1. Görüntü ve duruş (fragman kareleri ile Aedir ekran görüntüleri)

| | Half Sword | Aedir şu an |
|---|---|---|
| Kamera | Omuz üstü, yakın (1,5–2 m), yüksek; aşağı ~30–40° bakar. Karakter kadrajın sol-ortasında ve kadrajın büyük kısmını kaplar. | Uzak, gövde hizasında, düz bakıyor; karakter küçük ve ortada. |
| Bacaklar | Geniş açık, dizler belirgin bükülü, ağırlık aşağıda. | Dar, dizler neredeyse düz, dik duruş. |
| Silahlı el | Omuz/baş hizasında, dirsek bükülü ve yukarıda. Silah ya yukarı-geride (vom Tag) ya rakibe dönük (ochs/pflug). | Kol aşağıda sarkık ya da öne düz uzanmış. |
| Boş el | Göğüs önünde hazır, ya da iki elli silahta kabzada. | Yanda sarkık. |
| Gövde | Silah tarafına dönük, hafif öne eğik. | Dümdüz karşıya. |

## 2. Beden (aktif ragdoll) [G]
- Kemiklerin hiçbiri anahtar karelerle canlandırılmıyor; tüm hareket prosedürel. Her uzuv iki kemikli IK ile sürülüyor. Görünmeyen bir hedef iskelet, eklem motorları üzerinden fiziksel bedeni çekiyor.
- Leğen hareket kapsülüne serbest bir kısıtla bağlı; doğrusal ve açısal motorlar onu dik tutuyor ("lastikle bağlı").
- Adım atılacak yer önce zemine ışın atılarak bulunuyor, ayak oraya bir eğri boyunca gidiyor.
- Silah savrulurken ya da savuşturma yapılırken kollar biraz sertleşiyor.
- 0.6.1: bilekler sertleştirildi, silah daha az sallanıyor. Karakter kendi silahıyla çarpışmıyor.
- Düşmek zor, kalkmak hızlı (Mayıs 2025). Yerdeyken eller kontrolde kalıyor ve yerden dövüşmek mümkün. Kalkmak bacak ya da kafa yaralıysa yavaşlıyor.
- Bilinç kaybında beden gevşiyor. Boğulan biri önce bayılıyor, sonra ölüyor.

**Aedir:** Temel aynı (hedef poz, motorlar, leğen kısıtı, prosedürel adım). Eksikler:
- dövüş duruşu
- yerden dövüşme
- bilinç ve bayılma
- kalkma hızının yaraya bağlı olması

## 3. Kontroller

| Eylem | Half Sword (Early Access) | Aedir |
|---|---|---|
| Savuruş | Tuş basılıyken fare yönü silahı sürer. LMB sağ el, RMB sol el. | Fareyle sürülen kollar var. |
| İki elle tutma / yarım kılıç | LMB+RMB basılı ya da RMB tıkla. 0.6.1.4'ten beri "klasik" seçeneği var. | Yarım kılıç var. |
| Saplama | Alt'a dokun: klasik saplama, fare yalnız saplama yolunu sürer. Alt basılı: başparmak tutuşu. Ya da RMB çift tık. | Kısmen var. |
| El değiştir / silahı çevir | X. Basılı tutunca silah döner: kılıç ters tutuş, balta arka yüz. Yarım kılıçtan basılı tutunca mordhau. | Yok. |
| Tutma | E/Q. Basılı tutunca silah kına girer. Rakibin boğazı, kolu, zırhı ya da silahı tutulabilir. | Tutma ve itme var, boğaz ve zırh çekme yok. |
| Tekme | Space (yerden de yapılabiliyor). | Var. |
| Kilitlenme / görüş | Z kilitlenir, V birinci/üçüncü şahıs arasında geçer. | Kilitlenme var. |

## 4. Hasar [G]/[T]
- **Hasarın kaynağı:** Hasar tamamen fizikten gelir: silah kütlesi × hız ve vücut momentumu.
  - Silahın hangi kısmının çarptığı önemli: ağız, uç, düz yüz, topuz ya da siper. 0.6.1.1'de topuz ve siper darbeleri güçlendirildi.
  - Hayati organlara daha çok, diğer bölgelere daha az hasar gider. Ayrı bir karaciğer bölgesi var.
- **Üç ölçü [T]:**
  - Can 100: çoğunlukla kanamayla azalır.
  - Dayanıklılık 100: ekranda gösterilmez; taşınan ağırlıkla artar.
  - Bilinç 100: baş darbeleriyle düşer, düştükçe savuruş, denge ve hareket kötüleşir, zamanla geri gelir.
- **Ölüm:** Zırhsız bir dövüşçü tek vuruşta ölebilir. Oyuncu "son nefesine kadar" kontrol eder; ikiye bölünen biri bile hemen ölmez (0.6.1.5).

**Aedir:** kinetik enerjiyle hasar var. Eksikler:
- bilinç ve bayılma
- organ bölgeleri
- topuz ve siper darbesinin ayrı hesaplanması

## 5. Zırh
- **Parçalar:** Miğferin göz yarığından saplanabilir [G]. Açıklıklar: vizör, boyun (miğferle göğüslük arası), koltuk altı, kasık, kol ve bacak içi, göğüslük takılıysa sırt. Bevor boğmayı engeller [T].
- **Plaka:** Kesmeye karşı "çoğunlukla geçilmez" [T]. Kesik sekiyor; buna ayrı bir kural yok, fizikten kendiliğinden çıkıyor (?). Plaka, ezici darbenin geri itmesini durdurmaz [T].
- **Göçme ve hasar:** Zırh ve giysi hasarı var (Kasım 2024) [G]. Göçük olarak mı yırtık olarak mı göründüğü ve korumayı düşürüp düşürmediği açıklanmamış (?).
- **Çıkarma:** Miğfer darbeyle düşürülebilir, tutulup çekilebilir. Ölünün zırhı soyulabilir.
- **Ağırlık:** Ağır zırh hareketi yavaşlatır; güç arttıkça bu ceza azalır. Tam zırhlıyı yere düşürmek daha zor. Zırh giymek kas geliştirir.

**Aedir:** Bölge başına düz bir koruma oranı var. Eksikler:
- parça parça kaplama ve açıklıklar
- kesiğin plakadan sekmesi
- göçme
- miğferin düşmesi ya da çekilip alınması
- zırh ağırlığının hıza etkisi

## 6. Kesme, saplama, saplanıp kalma
- **Kesme:** Kesmek için ağzın doğru açıyla gelmesi gerekir; yassı vuruş kesmez. Kesik deri ve kastan kemiğe kadar katman katman ilerler, kemikte durur. Çekme kesik etkilidir (0.6.1.5).
- **Saplanıp kalma:** Silah tahtaya saplanır (0.6.1.5) [G]. Gövdede saplı kalan bir balta gözlemlenmiş [T] ama resmî bir kural açıklanmamış (?).

**Aedir:** Kesme, delik ve uzuv koparma var. Eksikler:
- katman derinliği
- silahın saplanıp kalması

## 7. Yaralar, kan, kemikler
- **Kan:** Yaradan aşağı akar, gölcük yapar, morluk oluşur (kırmızıdan mora döner). Giysi ve deri kana bulanır; kan, çıkartma (decal) yerine doğrudan yüzeye boyanıyor. Duvar ve zemine sıçrar, silahta iz bırakır [G].
- **Kesik ve uzuv kaybı:** Kesik her yerde olabilir ve tekrarlanabilir. Uzuv bir şeritle sarkık kalabilir. Parmaklara yakın kesik bütün eli götürür.
- **Kemikler (0.6.1.3):** Eklem çıkar; çekme ya da bükmeyle kemik kırılır (boyun, sırt, kol, bacak). Acı yapay zekâyı teslim ettirir. Kırık ya da yaralı uzuv gevşer ve kontrolü zorlaşır.

**Aedir:** Yara izi, kanama, damla, sıçrama ve uzuv koparma var. Eksikler:
- bedende aşağı akan kan
- morluk rengi
- kırık ve çıkık
- sarkık uzuv

## 8. Savunma
- **Savuşturma:** Silahı gelen darbeye açılı tutmak küçük bir otomatik sapma verir. Karşı savuruş çarpışma yaratır. Silah bağlama tamamen fizikten çıkar.
- **Silah düşürme:** Silah çekip alınabilir, dönen tekme silahı düşürür.
- **Kalkanlar:** Buckler, heater, pavise. Kalkan sol elle yönlendirilir; tekme kalkanı iter.

## 9. Yapay zekâ ve oyun döngüsü
- **Yapay zekâ:** Bastırır; savuşturur, karşılık verir, yan ve geri adım atar. Kötü donanımlı olan iyi donanımlıya saldırmaz. Acı ya da silahsızlık teslim ettirir. Boğulunca karşı koyar.
- **Rütbeler:** Dilenci → köylü → … → şövalye → lord → baron. Turnuvalar, bahis, tüccar, silah ve zırh ustası, paralı asker vardır. Ölünce "Abyss" modu başlar.

## Önerilen sıra (en büyük his farkından başlayarak)
1. Kamera ve dövüş duruşu: yakın, yüksek omuz üstü kamera; geniş bükülü bacaklar; silah yukarıda, dirsekler bükülü.
2. Bilek sertliği, silahın kendi bedene takılmaması, savuruşun takılmaması (bekleyen hata).
3. Zırh kaplaması ve açıklıklar, kesiğin plakadan sekmesi, ezici darbenin içeri geçmesi.
4. Bilinç ve bayılma; boğaz ve kol tutma, boğma.
5. Bedende akan kan, morluk; kırık, çıkık ve sarkık uzuv.
6. Silahı çevirme (X): ters tutuş, mordhau, balta arka yüzü. Saplama modu (Alt).
7. Zırh göçmesi ve miğferin düşmesi, saplanıp kalma.
8. Rütbe ve turnuva döngüsü.
