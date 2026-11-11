# Half Sword dövüş belgesi: savuruş, tutuş, darbe tepkisi, saplanma

Bu belge `MECHANICS_REFERENCE.md`'yi tamamlar. Odak altı konudur: iki elli tutuş, savuruş açıları ve hareket aralığı, darbe tepkisi ve denge, hasarın işlenmesi, saplanma ve zırh oturuşu.

Kaynak olarak yalnızca herkese açık bilgiler kullanıldı (aşağıda liste var). Hiçbir oyun dosyası, kodu ya da modeli açılmadı. Amaç aynı hissi kendi kodumuzla kurmak.

Etiketler:
- **[G]** geliştirici açıklaması ya da resmî sayfa
- **[T]** topluluk rehberi ya da forum
- **[V]** oynanış videosu ve fragman gözlemi
- **(?)** belirsiz

## 1. Temel ilke: fare silahı değil eli sürer [G]

- **Elin hareketi:** "Fare kılıcı doğrudan sürer" [G], ama sürülen şey elin hedefidir. Silah, ağırlığıyla ve bilekten gelen tork ile ona fiziksel olarak uyar. LMB basılıyken fare hareketi sağ kolu, RMB sol kolu sürer [T].
- **Tuş bırakılınca:** Kol, kameraya bakan nötr bir korumaya döner [V].
- **Animasyon yok:** Bütün beden, uzuvlar ve silahlar simüle edilir, anahtar kareyle canlandırılmaz [G]. Momentum, ağırlık ve denge çarpışmanın sonucunu belirler [G].
- **Vuruş kutusu yok:** Kesik, ağzın gerçekten değdiği yerde olur [G][T]. Bu yüzden görülen temas ile hasar her zaman örtüşür.
- **Fare ayarı:** Hassasiyet düşük, işaretçi ivmesi kapalı tavsiye edilir. Hızlı ve geniş fare hareketi, geniş ve güçlü savuruş demektir [T].

## 2. Savuruş açıları ve hareket aralığı

Videolardan gözlenen davranış [V], rehberlerle uyumlu [T]:

**Hazırlık (wind-up)**
- Fare bir yana çekilince el omuz hizasına ve **omzun gerisine** gider.
- Bilek geriye kırılır ve kılıç omzun üstünden **arkaya**, sırta doğru yatar.
- Gövde aynı yöne 45–60° döner, ağırlık arka ayağa geçer.
- Rehberlerin tarifi: "fareyi sağa sar, sonra sola kırbaçla" ağır kesiktir; hazırlıksız hızlı hareket hafif kesiktir [T].

**Tepeden vuruş**
- Fare yukarı çekilince eller başın üstüne çıkar.
- Kılıç başın arkasına, sırta doğru düşer (vom Tag / zornhut benzeri).
- Fare aşağı "çarpılınca" tepeden iner [T].

**Vuruş anı**
- El ön tarafa gelirken bilek açılır ve kılıç kolun devamı olur. En uzun erişim ve en yüksek uç hızı buradadır.
- Uç hızı, yalnız elin değil, bileğin açılmasından ve gövde dönüşünden de gelir. Kılıcın ucu ~270°'lik bir yay çizer.

**Takip (follow-through)**
- Vuruş ıskalanırsa silah karşı kalçaya ya da karşı omzun gerisine kadar gider.
- Burası aynı zamanda ters yöndeki ikinci hazırlıktır: ters omuz üstünden geri gelen vuruş [V].

**Bilek**
- Hazırlıkta ~90–120° kırılır, vuruşta düzleşir.
- 0.6.1'de bilekler sertleştirildi, silah daha az sallanır [G].

**Gövde ve kalça**
- Savuruşun büyük kısmı gövdeden gelir. Gövde kolla birlikte döner, omuz öne atılır.
- Koşarken ya da Shift atılımıyla vurmak darbeyi güçlendirir [T].

**Saplama**
- Alt tuşu ya da çift tıklama saplama modudur. Kılıç düzleşir, fare ileri itince uç ileri gider [T].
- Hızlı bir fare kırbacıyla saplama modundan çıkılır. Iskalanan bir saplama geri çekilirken kesik olarak kullanılabilir [T].

**Aedir şu an**
- El omuz çevresinde dışa yalnızca 72°, içe 50° dönebiliyor.
- El başın üstüne çıkamıyor (yükseklik sınırı +60°).
- Kılıç kol çizgisinin hep 50° üstünde sabit duruyor; bilek kırılmıyor.
- Sonuç olarak uç yalnızca elin küçük yayını izliyor. Kullanıcının tarifi tam bu: "sopanın ucunu dürtüyor gibi".

**Aedir'e uyarlama**
- **El aralığı:** El dışa 105°, içe 80° dönecek. Yükseklik −50° ile +82° arasında olacak (baş üstü dahil).
- **Bilek kırılması:** El yanlara ya da yukarı açıldıkça kılıç kol çizgisinden ayrılıp omzun ya da başın gerisine yatacak. El öne geldikçe kılıç düzleşip kolun devamı olacak.
- **Hazırlık açısı elin yüksekliğine bağlı:**
  - Alçak el: uç geride ve aşağıda (kuyruk duruşu).
  - Omuz hizası: uç omzun üstünden geride.
  - Baş üstü: uç başın gerisinde ve aşağıda.
- **Gövde dönüşü:** Payı 0,5'ten 0,6'ya, üst sınırı 40°'den 55°'ye çıkacak.
- **Ağırlık gecikmesi:** Mevcut ağırlıklı silah yönü dinamiği korunacak.

## 3. Tutuşlar

| Tutuş | Half Sword | Nasıl |
|---|---|---|
| Tek el | El kabzada, işaret parmağı siperin hemen altında | Varsayılan |
| İki el | Üst el siperin hemen altında, alt el onun altında (iki yumruk yan yana, aralarında boşluk yok). Kabza kısaysa alt el topuza yakın oturur. | LMB+RMB ya da iki elli silah [T] |
| Yarım kılıç | Bir el kabzada, diğeri bıçağın ortasında | Boş el varken RMB [T] |
| Mordhau / mordschlag | İki el bıçakta, siper ve topuzla vurulur | Yarım kılıçta X basılı (0.6.1) [T] |
| Ters tutuş, balta arka yüzü | Silah elde döner | X basılı [G] |
| Kalkan | Sol ön kolda; RMB ile gelen darbeye doğru yönlendirilir | [T] |

**Aedir'deki hata**
- Çelik uzun kılıçta iki yumruk kabzada yalnızca ~5 cm aralıkla iç içe duruyor; gerçekte bir yumruk ~9 cm'dir. Üst el siper sınırına dayandığı için alt el, kabzada aşağı inmeden üst elin içine giriyor.
- Kol çözücüsü iç içe yumrukları ayırmak için bileği büküyor ve kılıç yamuk duruyor.

**Uyarlama**
- İki elle tutulunca taşıyan el kabzada topuza doğru kayacak. Böylece iki yumruk arasında bir yumruk boyu kalacak.
- Kayma kabza uzunluğunun yarısını geçmeyecek.
- Tek ele dönünce el yerine geri dönecek.

## 4. Darbe tepkisi ve denge

**Half Sword'da**
- Hafif kesik zırhsızı sendeletir. Ağır kesik ve tepeden vuruş yere serebilir [T].
- Yere düşmek kolaydır; Erken Erişim'de karakterler daha hafif, daha sık tökezliyor [T].
- Tekme dengeyi bozar, sersemletir, kalkanı iter. Kasığa tekme düşürür [T].
- Vurulan uzuv vuruş yönünde savrulur ve gövde döner. Vurulan bir an kontrolünü kaybeder: kollar düşer, koruma açılır, gövde geri yaylanmaz [V].
- Ağır darbe yiyen kişi geri adım atarak dengesini toplamaya çalışır. Toplayamazsa düşer [V].
- **Tutuş kaybı:** Dengesi bozulan, bilinci giden ya da yeterli kuvvet yiyen silahı bırakır [T].
- **Zırh:** Ağır zırhlıyı düşürmek daha zordur. Ezici darbe plakadan geçer [T].
- **Knockdown modu:** Rakibi ayaklarından düşürmek tek başına bir zafer koşuludur [T]. Bu, düşmenin oyunun merkezinde olduğunu gösterir.

**Aedir şu an**
- Darbe enerjisinin yalnızca %35'i dengeye gidiyor. Gövde için referans 400 J, sendeleme eşiği 0,25.
- Tipik bir 150–250 J kılıç darbesi bu yüzden 0,13–0,22 denge kaybı veriyor ve sendeletmiyor.
- Sendelerken hız %80'de kalıyor, bu yüzden yapay zekâ yürümeye devam ediyor.
- Kemiğe verilen itme küçük. Eklem motorları tam güçte kaldığı için beden hemen geri yaylanıyor.

**Uyarlama**
- **Gevşeme (flinch):** Her gerçek darbede beden kasları darbe gücüne göre ~0,45 sn gevşeyecek (en çok %55). Böylece beden yaylanmak yerine darbeye boyun eğecek.
- **Daha büyük itme:** Kemiğe ve gövdeye giden itme büyüyecek.
- **Denge:**
  - Dengeye giden pay %60 olacak.
  - Bölge referansları düşecek: baş 110, gövde 260, kol 420, bacak 170 J.
  - Sendeleme eşiği 0,15 olacak.
- **Sendeleme:**
  - 0,5–1,4 sn sürecek, geri itiş daha güçlü olacak.
  - Sendeleyen hızının yalnızca %25'iyle yürüyebilecek; yapay zekâ ilerleyemeyecek.
- **Düşme eşikleri düşecek:** baş 300, gövde 600, bacak 260, kol 1200 J. Biriken denge kaybı da zaten düşürüyor.

## 5. Hasarın işlenmesi

**Half Sword'da**
- Hasar tamamen fizikten gelir: kütle × hız, çarpan kısım (ağız, uç, yassı, topuz), bölge, zırh [G][T].
- Temas varsa hasar vardır. Çekme kesik de hasar verir (0.6.1.5) [T].

**Aedir'de bulunan nedenler (log ve kod incelemesi)**
1. **Kol "etkin" değilken vuruş sayılmıyor.** Darbe taraması yalnızca koruma ya da savuruş tuşu basılıyken çalışıyor. Tuşu bırakıp savurmaya devam eden ya da silahı taşırken çarpan kılıç hiç hasar vermiyor.
2. **Aynı rakibe 0,3 sn bekleme süresi.** Koldan gövdeye geçen tek bir kesikte gövde vuruşu sayılmıyor, üstelik kılıç bedende durduruluyor.
3. **Yassı hasar iki kez ölçekleniyor.** Hafif ve orta darbelerde hasar, enerji katsayısının karesiyle küçülüyor. Örnek: 69 J'lik bir vuruş yalnızca 2,4 hasar verdi.

**Uyarlama**
1. Hızlı giden (≥400 cm/sn) elde tutulan silah, tuş basılı olmasa da vurur.
2. Bekleme süresi rakip başına değil, rakip ve kemik başına olacak. Bir kesik koldan gövdeye geçerse ikisi de sayılır.
3. Yassı hasar katsayısı bir kez uygulanacak.

## 6. Saplanma

**Half Sword'da**
- Sert vuruşta silah bedene saplanabilir; geliştiriciye göre bu kasıtlıdır [G].
- Saplanan silahı çıkarmak için fareyle çekip sökmek gerekir. Alternatifler:
  - çift tıklamak (saplama hareketi, anında çıkarır) [G][T]
  - silahı bırakıp sonra cesetten almak (E/Q) [T]
- Rehberlere göre saplanma anında bitirme fırsatı yaratır [T].
- Silahla sabitlenen (pinned) yapay zekâ teslim olur [T].

**Aedir şu an**
- Saplanma yalnızca hedefi geçemeyen uç ya da balta ağzı vuruşlarında oluyor.
- Silah 0,12–0,45 sn içinde kendiliğinden çıkıyor.
- Hedefi delip geçecek kadar güçlü bir saplama "delip geçti" sayılıyor ve hiç saplanmıyor.
- Kurban, saplı silahtan hiçbir çekme hissetmiyor.

**Uyarlama**
- **Uçla vuruş her zaman saplanır:**
  - Sekmediği sürece uçla gelen vuruş saplanır.
  - Hedefi geçecek kadar güçlü olan saplama daha derine girer, bıçak boyunun en çok yarısına kadar.
- **Sökme:**
  - Silah ancak çekilerek sökülür.
  - Sökmek için gereken çekme mesafesi derinlikle artar: 14 cm + derinliğin yarısı.
  - Güvenlik süresi en çok 4 sn.
  - Sökülürken yara açılır.
- **Kurbanı sürükleme:**
  - Silah saplıyken saldıranın çekişi kurbanın o kemiğini çeker ve dengesini bozar.
  - Kurban saplı bıçakla sendeler.

## 7. Zırh oturuşu: omuzluk

Kullanıcı gözlemi: omuz, omuzluğun arka tarafından dışarı taşıyor.

Uyarlama:
- Omuzluk modeli %15 daha uzun (64 → 74 cm), yani daha geniş yapılacak.
- Biraz arkaya kaydırılacak: offset X 6 → 3 cm. Böylece omzun arka yüzü de kapanır.

## Kaynaklar

- **Resmî:** [halfswordgames.com: oyun sayfası](https://halfswordgames.com/games/halfsword/) [G], [Half Sword (Wikipedia)](https://en.wikipedia.org/wiki/Half_Sword)
- **Steam rehberleri:**
  - [Half Sword Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=3661780631) [T]
  - [How to play Half Sword PlayTest](https://steamcommunity.com/sharedfiles/filedetails/?id=3448761291) [T]
- **Steam tartışmaları:**
  - [Weapon stuck in the body](https://steamcommunity.com/app/2397300/discussions/0/599640944183092342/), geliştirici yanıtı içerir [G][T]
  - [Basic Controls/Mechanics/Tips](https://steamcommunity.com/app/2397300/discussions/0/762932533852698645/) [T]
  - [How do you mordhau?](https://steamcommunity.com/app/2397300/discussions/0/565913205989471381/) [T]
- **Rehber siteleri:**
  - [TheGamer: tüm kontroller](https://www.thegamer.com/half-sword-complete-mechanics-controls-guide/) [T]
  - [GameRant: oyunun öğretmediği mekanikler](https://gamerant.com/half-sword-mechanics-game-doesnt-teach-how-kick-block/) [T]
  - [BlogAndGuide: başlangıç rehberi 2026](https://www.blogandguide.com/half-sword-beginner-guide-2026/) [T]
  - [Erken Erişim yeni mekanikleri](https://half-sword.pages.dev/blog/new-mechanics-early-access) [T]

## Observed while playing Half Sword (2026-11-11)

Controls, tried in the game itself:
- Mouse with no button held turns the view and the body with it; the weapon stays in its carried pose (two-handed sword: hands before the hips, point low forward to the right).
- Left button held: the mouse drives the hands, the view stays still. The hands keep where the mouse left them; letting go returns the weapon to the carried pose within about half a second.
- The mouse moves the hands over a very wide field: about 220 px of mouse travel takes the hands from the hips to above the head; sideways the arm straightens fully out at shoulder height (to the right the sword stands upright in the outstretched hand; to the left the arm crosses the body and straightens out, blade aslant up-left). There is no narrow box: the hands go as far as the arms reach.
- The blade's attitude follows from where the hands are: in the middle the point is on the enemy; out at the sides and up it stands upright or leans back over the shoulder; low it hangs.
- Up and then right: the hands at the right shoulder, the blade across over the head up to the left. From there left again: the blade upright behind the head.
- Big sudden mouse jumps pin the hands at the edge of their field; motion is continuous with the mouse.
- Alt held: the sword is drawn back upright before the right shoulder, point forward-up (thrust ready).
- Ctrl: the fighter lies down. Shift+W from the ground: gets up through physics, pushing off with a hand.
- Q: the weapon is brought up upright before the body. E: lets go of / takes the weapon.
- The AI with a two-handed sword thrusts from range with one arm straight out and the blade level, the other hand off the grip.

### Pose grid with the left button held (split screen, an idle second player watching from the front)

Mouse moved smoothly from the middle to 25 points (x -450..+450 px, y -300..+300 px), two-handed sword:
- Middle (button just pressed): hands before the chest, a little right; the point forward on the enemy.
- Middle row, right: the hands go to the right of the chest and on to the arm's full length out right at shoulder height; the point stays on the enemy as long as it can, so the blade crosses in front of the body to the left; at the far right the arm is straight and the blade stands upright in the hand.
- Middle row, left: the hands cross to the left side; half way the blade points forward-left; at the far left the hands are by the left shoulder and the blade lies back over it (wind-up).
- Upper rows: the hands rise only to the shoulders and face; with the hands up on the left the blade hangs point down before the body (hanging guard); up in the middle it points level across to the left; up on the right it stands upright by the right shoulder, arm straight.
- Lower rows: the hands drop to the belly and hips; low in the middle the point is low forward / across to the left; low right the hands are at the right hip with the blade rising aslant across; low left the blade hangs down by the left leg.
- With the button held the whole body turns with the weapon: chest, hips and feet go round with the hands, so a far sweep turns the fighter a quarter turn or more.
- Big jumps of the mouse pin the hands at the edge; the field is wide but bounded by the arm's reach.
