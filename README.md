# HRM SmartFan v2: sterownik wentylatora treningowego DIY

Zbudowałem ten sterownik pod trenażer rowerowy (Zwift, Rouvy, MyWhoosh) oraz bieżnię. Całość działa na mikrokontrolerze **ESP32-S3**, okrągłym ekranie **GC9A01 240x240**, module regulatora fazowego **RobotDyn AC Dimmer (Z-C / PSM)** i pasie tętna **BLE Heart Rate Service (HRS)**.

Układ płynnie steruje obrotami zwykłego wentylatora 230 V AC na podstawie aktualnego tętna (5 stref BPM z histerezą i filtrem) albo w trybie ręcznym (`MANUAL`). Za ułamek ceny gotowych wentylatorów smart, takich jak Wahoo Headwind, możesz złożyć własny, cichy nawiew, który sam reaguje na zmęczenie.

---

## Co potrafi sterownik

- **Łączy się z dowolnym pasem tętna BLE (HRS `0x180D`).** Przetestowałem go z **Garmin HRM-Pro+**, ale kod obsługuje też pasy i opaski Polar, Wahoo TICKR, Coospo, Magene, Decathlon, Suunto czy Coros. Po pierwszym sparowaniu płytka sama wznawia połączenie w tle.
- **Ma wbudowany panel na telefon (iOS i Android) oraz obsługuje domowe Wi-Fi (`http://smartfan.local`).** Sterownik wystawia własną sieć Wi-Fi **`SmartFan`** pod adresem **`http://192.168.4.1`**. Gdy wybierzesz z listy swój domowy router, połączy się z nim i będzie dostępny pod adresem **`http://smartfan.local`**, więc podczas jazdy na telefonie nadal działa choćby *Zwift Companion*. W panelu możesz:
  - sprawdzać na żywo tętno (`BPM`), moc nawiewu (`%`) i stan połączenia z pasem,
  - przełączać tryby **`OFF` / `AUTO (HR)` / `MANUAL`**, zmieniać bieg suwakiem albo jednym dotknięciem na czterech dużych kafelkach szybkich biegów (**`30% / 50% / 75% / 95%`**),
  - wybierać między **płynną krzywą liniową** (sterownik sam wylicza każdy pojedynczy procent mocy pomiędzy strefami, podobnie jak Wahoo Headwind) a klasycznym trybem **5 biegów schodkowych**,
  - ustawić **podtrzymanie nawiewu po wysiłku (*Cooldown*, domyślnie `60 s`)**, żeby po zejściu tętna poniżej strefy `Z1` wentylator chłodził Cię jeszcze przez wybrany czas na pierwszym biegu,
  - wyszukać pasy HR w pobliżu i sparować wybrany pas jednym kliknięciem,
  - zmienić i zapisać w pamięci NVS progi 5 stref tętna (`BPM -> %`), histerezę, filtr oraz tempo przyspieszania i zwalniania nawiewu,
  - wyszukać z listy swoje domowe Wi-Fi i połączyć się z nim bez ręcznego wpisywania nazwy sieci (jeśli kiedykolwiek zablokujesz dostęp, przytrzymaj przycisk `CONFIG` w trybie `KONFIGURACJA`, a sieć `SmartFan` znów stanie się otwarta),
  - **wgrywać aktualizacje oprogramowania przez Wi-Fi (Web OTA) wprost z telefonu lub komputera**, bez podłączania kabla USB i bez utraty zapisanych ustawień.
- **Szybko wchodzi na obroty i wolniej z nich schodzi (*Fast Attack / Slow Decay*).** Gdy tętno rośnie na podjeździe lub podczas interwału, filtr reaguje czterokrotnie szybciej. Kiedy skończysz mocny odcinek i tętno spada, układ dłużej utrzymuje nawiew, żeby schłodzić rozgrzany organizm.
- **Działa od razu po wgraniu.** Przy pierwszym uruchomieniu na czystym ESP32-S3 program sam zapisuje w pamięci NVS domyślne strefy tętna.
- **Bezpiecznie steruje fazą 230 V AC (50 Hz):**
  - wykrywa przejście przez zero (`Z-C`) i wyzwala triak (`PSM`) za pomocą sprzętowego timera 64-bitowego `TIMERG1/T0` (`1 MHz` z `XTAL 40 MHz`), a przerwania trzyma w pamięci **IRAM / DRAM**,
  - odrzuca zakłócenia z sieci (okno półokresu od `9000` do `11000 µs` z licznikiem błędów miękkich *Leaky Bucket*, który pomija pojedyncze zakłócone półokresy bez wyłączania nawiewu), podbija moc przy starcie silnika (`60%` przez `500 ms`) i wygasza wyjście w 30 ms, gdyby pętla sterująca stanęła.
- **Obsługuje fizyczne przyciski i okrągły ekran GC9A01.** Wszystkim posterujesz też przyciskami na obudowie. Przycisk `PAIR` ma zawsze najwyższy priorytet i natychmiast wyłącza nawiew (`OFF`).

---

## Lista części (BOM) i schemat połączeń

### Potrzebne części
1. **Mikrokontroler:** płytka `ESP32-S3` (np. `ESP32-S3-DevKitC-1` N8 lub N16, 8 MB Flash).
2. **Wyświetlacz:** okrągły ekran IPS `GC9A01 1.28" 240x240 SPI` (3,3 V).
3. **Moduł wykonawczy AC:** `RobotDyn AC Light Dimmer Module` (z wejściem `PSM` i detekcją zera `Z-C`).
4. **Przyciski:** 2 przyciski chwilowe (tact-switch lub przyciski obudowy) zwierające do `GND` (`PAIR` oraz `CONFIG`).
5. **Wentylator:** zwykły wentylator stojący lub podłogowy 230 V AC, 50 Hz (silnik indukcyjny 45 do 70 W, przełączony mechanicznie na najwyższy bieg).

### Wyprowadzenia (Pinout ESP32-S3)

| Podzespół | Sygnał | Pin ESP32-S3 | Uwagi |
|---|---|---:|---|
| **Ekran GC9A01 (SPI)** | `SCK` (`SCL`) | **GPIO 10** | Zegar SPI2 |
| | `MOSI` (`SDA`) | **GPIO 11** | Dane SPI2 |
| | `CS` | **GPIO 9** | Chip Select |
| | `DC` | **GPIO 8** | Data / Command |
| | `RST` (`RESET`) | **GPIO 14** | Reset ekranu |
| | `BL` (`BLK`) | **GPIO 2** | Podświetlenie (aktywne stanem wysokim) |
| | `VCC` / `GND` | `3V3` / `GND` | Zasilanie 3,3 V |
| **Moduł RobotDyn AC** | `Z-C` (Zero-Cross) | **GPIO 4** | Wejście detekcji zera (`INPUT_PULLUP`, zbocze narastające) |
| | `PSM` (`GATE` / `PWM`) | **GPIO 18** | Wyjście impulsów na bramkę triaka (`pull-down` przy starcie) |
| | `VCC` / `GND` | `3V3` / `GND` | Logika 3,3 V (transoptory na module) |
| **Przyciski i dioda** | Przycisk `PAIR` | **GPIO 12** | Zwiera do `GND` (`INPUT_PULLUP`) |
| | Przycisk `CONFIG` | **GPIO 16** | Zwiera do `GND` (`INPUT_PULLUP`) |
| | Dioda `LED` | **GPIO 13** | Świeci, gdy nawiew pracuje |

> **Podłączenie strony 230 V AC w module RobotDyn (złącze śrubowe `IN` / `OUT`):**
> Do wejścia `AC-IN` podłącz zarówno przewód fazowy (`L`), jak i neutralny (`N`). Bez przewodu neutralnego transoptor na płytce nie wykryje przejścia przez zero i nie wyśle impulsów `Z-C (100 Hz)`.

---

## Szybkie wgranie przez Arduino IDE

1. Zainstaluj **Arduino IDE 2.x**.
2. W menedżerze płytek (*Tools -> Board -> Boards Manager*) zainstaluj pakiet **`esp32 by Espressif Systems`** w wersji **`3.3.5`** (biblioteka `NimBLE-Arduino 2.3.7` wymaga struktury kontrolera BT z ESP-IDF v5.5 użytej właśnie w wersji `3.3.5`).
3. W menedżerze bibliotek (*Tools -> Manage Libraries*) zainstaluj:
   - **`NimBLE-Arduino`** w wersji **`2.3.7`**,
   - **`LovyanGFX`** w wersji **`1.2.19`**.
4. Otwórz szkic [`firmware/SmartFan/SmartFan.ino`](firmware/SmartFan/SmartFan.ino).
5. Wybierz płytkę **`ESP32S3 Dev Module`** (*Flash Size: 8MB*, *Partition Scheme: Default 8MB with spiffs*, *PSRAM: Disabled*) i kliknij **Upload (`->`)**.
6. W pliku [`firmware/SmartFan/src/Board.h`](firmware/SmartFan/src/Board.h) domyślnie włączyłem profil sprzętowy `#define FAN_AC_HARDWARE 1`. Jeśli chcesz przetestować sam ekran i menu na biurku bez podłączania modułu AC, zmień tę wartość na `0` (tryb `DRY-RUN`).

---

## Obsługa na co dzień

### 1. Panel w telefonie (iOS i Android)
1. Połącz telefon z siecią Wi-Fi **`SmartFan`** (jest otwarta i działa lokalnie bez internetu).
2. Panel wyskoczy sam albo otworzysz go w przeglądarce pod adresem **`http://192.168.4.1`**. Przy każdej sekcji i parametrze znajdziesz ikonę **`?`**, która po dotknięciu wyświetla okienko z prostym wyjaśnieniem danej funkcji.
3. Żeby sparować pas tętna, załóż go na klatkę, kliknij **„🔍 Skanuj pasy HR w pobliżu”**, a gdy pojawi się na liście, kliknij **„Połącz”**. Sterownik zapisze adres pasa w pamięci NVS i na kolejnych treningach połączy się z nim sam.
4. Wybierz tryb **`AUTO (HR)`** (bieg zależy od tętna) albo **`MANUAL`** (ustawiasz obroty suwakiem).

### 2. Po co i jak połączyć sterownik z domowym Wi-Fi (`http://smartfan.local`)?
Domyślnie telefon łączy się bezpośrednio z siecią wentylatora (`SmartFan` pod adresem `192.168.4.1`). Taka sieć działa w każdym miejscu (nawet w garażu czy piwnicy bez routera), ale telefon nie ma wtedy dostępu do internetu.

Jeśli podczas treningu używasz na telefonie aplikacji, które wymagają internetu lub domowej sieci (takich jak *Zwift Companion*, *Spotify* czy *YouTube*):
1. Na samym dole panelu `192.168.4.1` rozwiń zakładkę **„⚙️ Połączenie z domowym Wi-Fi (opcjonalne)”**.
2. Kliknij **„📡 Wyszukaj sieci Wi-Fi w pobliżu”** i wybierz ze znalezionej listy swój domowy router.
3. Wpisz hasło do swojego Wi-Fi i kliknij **„Połącz z wybraną siecią”**.
4. Od tej chwili Twój telefon może cały czas pozostawać w domowym Wi-Fi (masz internet i *Zwift Companion*), a panel wentylatora otworzysz w dowolnej przeglądarce pod adresem **`http://smartfan.local`** (lub po adresie IP wyświetlanym na pasku statusu). Własna sieć `SmartFan` (`192.168.4.1`) nadal działa w tle jako zapasowa.

### 3. Aktualizacja oprogramowania przez Wi-Fi (Web OTA)
Gdy pojawi się nowa wersja programu, nie musisz rozkręcać obudowy ani podłączać kabla USB do płytki:
1. Pobierz na telefon lub komputer plik **`SmartFan_OTA.bin`** z zakładki **Releases** na GitHubie. Jeśli kompilujesz kod samodzielnie w Arduino IDE, kliknij w menu *Sketch -> Export Compiled Binary*, a gotowy plik `.bin` pojawi się w folderze projektu.
2. Wejdź w panel sterownika (`http://192.168.4.1` lub `http://smartfan.local`) i rozwiń na samym dole zakładkę **„🔄 Aktualizacja oprogramowania (OTA)”**.
3. Wybierz pobrany plik `.bin` i kliknij **„⬆️ Wgraj aktualizację (.bin)”**.
4. Sterownik sam zatrzyma śmigło na czas zapisu, pokaże pasek postępu (`0 do 100%`) i po około 5 sekundach uruchomi się ponownie z nową wersją. Twój sparowany pas tętna, strefy `Z1 do Z5` oraz ustawienia domowego Wi-Fi zostają w pamięci NVS.

### 4. Co oznaczają poszczególne parametry w panelu?
- **Strefy Z1 do Z5 (`Od BPM` i `Moc %`):** dolne progi tętna i przypisana im moc nawiewu. Poniżej progu `Z1` wentylator stoi wyłączony (`0%`). Gdy tętno osiągnie dany próg, nawiew płynnie przechodzi na zadany procent mocy.
- **Tryb krzywej AUTO (`Płynny` lub `5 biegów`, domyślnie `Płynny`):** w trybie `Płynny` sterownik liniowo wylicza każdy pojedynczy procent mocy pomiędzy sąsiednimi progami stref (np. gdy Twoje tętno jest w połowie drogi między `Z1` a `Z2`, wentylator ustawi dokładnie `38%`), więc nie czujesz skokowych zmian biegów. W trybie `5 biegów` sterownik trzyma stałą moc danej strefy aż do przekroczenia kolejnego progu.
- **Podtrzymanie po wysiłku (`0 do 600 s`, domyślnie `60 s`):** gdy skończysz mocny podjazd albo zejdziesz z roweru i tętno spadnie poniżej progu `Z1`, ciało nadal oddaje dużo ciepła. Dzięki tej opcji wentylator nie wyłączy się od razu do zera, tylko będzie chłodził Cię na pierwszym biegu (`Z1`) jeszcze przez wybraną liczbę sekund.
- **Histereza (`1 do 15 BPM`, domyślnie `3 BPM`):** zapobiega ciągłemu skakaniu obrotów w górę i w dół, gdy Twoje tętno waha się dokładnie na granicy dwóch stref. Przykładowo przy progu `125 BPM` i histerezie `3 BPM` wentylator włączy wyższy bieg przy `125 BPM`, ale zwolni dopiero wtedy, gdy tętno spadnie poniżej `122 BPM`.
- **Filtr HR (`0 do 10000 ms`, domyślnie `2000 ms`):** wygładza chwilowe skoki odczytu z opaski. Filtr działa asymetrycznie: gdy zaczynasz mocny odcinek i tętno rośnie, reaguje czterokrotnie szybciej, a po zakończeniu interwału opada wolniej, żeby najpierw schłodzić rozgrzane ciało.
- **Wzrost i Spadek (`1 do 100 %/s`, domyślnie `15` i `8 %/s`):** określają, o ile punktów procentowych na sekundę śmigło przyspiesza i zwalnia przy zmianie biegu, dzięki czemu silnik zmienia obroty łagodnie i bez szarpnięć.
- **Domyślny MANUAL (`%`, domyślnie `45%`):** bieg, z jakim rusza wentylator po włączeniu trybu ręcznego fizycznym przyciskiem `CONFIG` na obudowie.

### 5. Fizyczne przyciski na obudowie

| Tryb aktualny | Krótkie `PAIR` (`GPIO 12`) | Przytrzymanie `PAIR` (`1,2 s`) | Krótkie `CONFIG` (`GPIO 16`) | Przytrzymanie `CONFIG` (`1,2 s`) |
|---|---|---|---|---|
| **OFF** | Włącz **AUTO** | **Wybór pasa HR** (skan BLE) | Włącz **MANUAL** | Otwórz **KONFIGURACJĘ BLE** |
| **AUTO** | **OFF** (natychmiast) | **OFF** | Brak akcji | Brak akcji |
| **MANUAL** | **OFF** (natychmiast) | **OFF** | Cel **+5%** | Cel **-5%** |
| **WYBÓR PASA** | Następny pas na liście | Wyjdź do **OFF** | Zatwierdź wybrany pas | Wyjdź do **OFF** |
| **KONFIGURACJA** | Wyjdź do **OFF** | Wyjdź do **OFF** | Brak akcji | **Usuń parowania BLE i hasło AP** |

### 6. Domyślne strefy tętna (tryb AUTO)

| Strefa | Zakres tętna | Nastawa wentylatora |
|---|---|---|
| Poniżej Z1 | `< 105 BPM` | `0%` (wyłączony) |
| **Z1** | `105 do 124 BPM` | `30%` |
| **Z2** | `125 do 144 BPM` | `45%` |
| **Z3** | `145 do 164 BPM` | `60%` |
| **Z4** | `165 do 199 BPM` | `75%` |
| **Z5** | `od 200 BPM` | `95%` |

*(Wszystkie progi BPM, moce w procentach, histerezę `3 BPM`, filtr `2000 ms` oraz szybkość rampy możesz zmienić w panelu `http://192.168.4.1` albo skryptem na Windows).*

---

## Konfigurator Windows CLI (opcjonalny)

Poza panelem Wi-Fi w projekcie znajdziesz też skrypt BLE dla Windows ([`client/smartfan.py`](client/smartfan.py)), który paruje się kodem PIN z ekranu i sprawdza sumy CRC-32:

```powershell
.\scripts\Prepare-Client.ps1
$py = '.\.local\venv\Scripts\python.exe'
& $py .\client\smartfan.py defaults .\ustawienia.json
& $py .\client\smartfan.py validate .\ustawienia.json
& $py .\client\smartfan.py scan
& $py .\client\smartfan.py set .\ustawienia.json
& $py .\client\smartfan.py get
& $py .\client\smartfan.py export .\kopia.json
& $py .\client\smartfan.py import .\kopia.json
```

---

## Budowanie z wiersza poleceń i testy

Kod C++20 ma zestaw testów jednostkowych i integracyjnych uruchamianych na komputerze z **UBSan** oraz **AddressSanitizer**, testy klienta w Pythonie i skrypt sprawdzający sekcje `IRAM`/`DRAM` w pliku ELF (`Inspect-Isr.ps1`):

```powershell
.\scripts\Prepare-Firmware.ps1
.\scripts\Prepare-Host.ps1
.\scripts\Test.ps1
.\scripts\Test.ps1 -AddressSanitizer
.\scripts\Build.ps1 -Profile both
.\scripts\Inspect-Isr.ps1
.\scripts\Package.ps1
```

Szczegóły techniczne opisałem w katalogu [`docs/`](docs/):
- [Architektura systemu i współbieżność](docs/ARCHITEKTURA.md)
- [Raport weryfikacji i powtarzalności kompilacji](docs/RAPORT_WERYFIKACJI.md)
- [Macierz wymagań i testów (T01 do T22, R01 do R06)](docs/MACIERZ_WYMAGAN_I_TESTOW.md)
- [Sprzęt, profil fazowy i procedury odbiorcze](docs/SPRZET_I_ODBIOR.md)
- [Specyfikacja protokołu BLE GATT](docs/PROTOKOL_BLE.md)

---

## Bezpieczeństwo i informacje prawne

### Napięcie sieciowe 230 V AC
W wariancie sprzętowym (`ac-hardware`) sterownik pracuje z regulatorem fazowym i wentylatorem zasilanym z sieci 220 do 240 V AC, 50 Hz. Wszelkie prace przy obwodach 230 V AC wykonuj przy odłączonym zasilaniu, dbając o izolację galwaniczną i zabezpieczenie nadprądowe (bezpiecznik topikowy w obwodzie zasilania wentylatora).

### Wyłączenie odpowiedzialności (AS IS)
Ten projekt stworzyłem w celach edukacyjnych i hobbystycznych. Urządzenie nie jest certyfikowanym wyrobem medycznym ani certyfikowanym sprzętem elektrycznym. Cały kod źródłowy oraz dokumentację udostępniam w stanie takim, w jakim się znajdują ("AS IS"), bez żadnych gwarancji. Nie odpowiadam za szkody wynikające z budowy, przerabiania lub używania tego układu.

### Prawa autorskie i biblioteki open-source
Kod źródłowy oraz dokumentacja projektu HRM SmartFan v2 należą do mnie. W projekcie skorzystałem z bibliotek open-source, do których prawa mają ich autorzy:
- `Arduino-ESP32` (Espressif Systems), licencja LGPL-2.1,
- `ESP-IDF` (Espressif Systems), licencja Apache-2.0,
- `NimBLE-Arduino` (h2zero / Apache Mynewt), licencja Apache-2.0,
- `LovyanGFX` (lovyan03), licencja BSD-2-Clause,
- `Bleak` oraz pakiety `winrt-*` (PyWinRT), licencja MIT.

### Znaki towarowe
Wymienione w dokumentacji nazwy handlowe i znaki towarowe (w tym Bluetooth, Windows, ESP32-S3, Garmin, Polar, Wahoo, RobotDyn, Android, iOS) należą do ich właścicieli i podałem je wyłącznie informacyjnie.
