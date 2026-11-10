# HRM SmartFan v2 – sterownik wentylatora treningowego DIY

Zbudowałem autonomiczny sterownik wentylatora pod trenażer rowerowy (Zwift, Rouvy, MyWhoosh) i bieżnię, oparty na mikrokontrolerze **ESP32-S3**, okrągłym wyświetlaczu **GC9A01 240×240**, module regulatora fazowego **RobotDyn AC Dimmer (Z-C / PSM)** oraz pasie tętna **BLE Heart Rate Service (HRS)**.

Układ płynnie reguluje obroty zwykłego wentylatora sieciowego 230 V AC na podstawie aktualnego tętna (5 stref BPM z histerezą i filtrem wykładniczym) albo w trybie ręcznym (`MANUAL`). Dzięki temu za ułamek ceny komercyjnych wentylatorów smart (takich jak Wahoo Headwind) można zbudować własny, cichy i w pełni konfigurowalny nawiew reagujący na wysiłek.

---

## Co potrafi sterownik

- **Działa z dowolnym pasem tętna BLE (HRS `0x180D`):** przetestowałem go z **Garmin HRM-Pro+**, a silnik BLE obsługuje również pasy i opaski Polar, Wahoo TICKR, Coospo, Magene, Decathlon, Suunto oraz Coros. Po jednorazowym sparowaniu sterownik sam łączy się z pasem w tle.
- **Wbudowany panel w telefonie (iOS / Android):** sterownik tworzy lokalną sieć Wi-Fi **`SmartFan`** z panelem pod adresem **`http://192.168.4.1`**. Z telefonu można:
  - podejrzeć na żywo tętno (`BPM`), aktualną moc nawiewu (`%`) i status połączenia z pasem,
  - przełączać tryby **`OFF` / `AUTO (HR)` / `MANUAL`** i płynnie zmieniać obroty suwakiem,
  - wyszukać pasy HR w pobliżu i sparować wybrany pas jednym kliknięciem,
  - dopasować i zapisać w pamięci NVS progi 5 stref tętna (`BPM -> %`), histerezę, stałą filtru oraz szybkość narastania i opadania obrotów.
- **Działa od razu po wgraniu:** przy pierwszym uruchomieniu na czystym układzie ESP32-S3 sterownik sam zapisuje w pamięci NVS domyślne strefy tętna.
- **Płynne i bezpieczne sterowanie fazowe 230 V AC (50 Hz):**
  - detekcja przejścia przez zero (`Z-C`) i wyzwalanie triaka (`PSM`) działają na sprzętowym timerze 64-bitowym `TIMERG1/T0` (`1 MHz` z `XTAL 40 MHz`) z obsługą przerwań w pamięci **IRAM / DRAM**,
  - układ odfiltrowuje zakłócenia sieciowe (okno półokresu `9000–11000 µs`), podbija moc przy starcie silnika indukcyjnego (`50%` przez `400 ms`) i wygasza wyjście w ciągu 30 ms w razie zatrzymania pętli sterującej.
- **Fizyczne przyciski i okrągły ekran GC9A01:** wszystkimi funkcjami można sterować także bezpośrednio przyciskami na obudowie, a przycisk `PAIR` zawsze działa jako natychmiastowy wyłącznik `OFF`.

---

## Lista części (BOM) i schemat połączeń

### Potrzebne komponenty
1. **Mikrokontroler:** płytka rozwojowa `ESP32-S3` (np. `ESP32-S3-DevKitC-1` N8 lub N16, 8 MB Flash).
2. **Wyświetlacz:** okrągły ekran IPS `GC9A01 1.28" 240×240 SPI` (3,3 V).
3. **Moduł wykonawczy AC:** `RobotDyn AC Light Dimmer Module` (z detekcją przejścia przez zero `Z-C` i wejściem bramki `PSM`).
4. **Przyciski:** 2× przycisk chwilowy (tact-switch / przycisk obudowy) podłączony do `GND` (`PAIR` oraz `CONFIG`).
5. **Wentylator:** klasyczny wentylator stojący/podłogowy 230 V AC, 50 Hz (silnik indukcyjny, np. 45–70 W, ustawiony mechanicznie na najwyższy bieg).

### Mapa wyprowadzeń (Pinout ESP32-S3)

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
| | `PSM` (`GATE` / `PWM`) | **GPIO 18** | Wyjście impulsów wyzwalających triak (`pull-down` przy starcie) |
| | `VCC` / `GND` | `3V3` / `GND` | Logika 3,3 V (transoptory na module) |
| **Przyciski i dioda** | Przycisk `PAIR` | **GPIO 12** | Zwiera do `GND` (`INPUT_PULLUP`) |
| | Przycisk `CONFIG` | **GPIO 16** | Zwiera do `GND` (`INPUT_PULLUP`) |
| | Dioda `LED` | **GPIO 13** | Sygnalizacja aktywnego wysterowania |

> **Podłączenie części sieciowej 230 V AC na module RobotDyn (złącze śrubowe `IN` / `OUT`):**
> Moduł wymaga podłączenia zarówno przewodu fazowego (`L`), jak i neutralnego (`N`) po stronie wejścia `AC-IN`, aby transoptor detektora przejścia przez zero mógł generować impulsy synchronizacji `Z-C (100 Hz)`.

---

## Szybkie wgranie przez Arduino IDE

1. Zainstaluj **Arduino IDE 2.x**.
2. W menedżerze płytek (*Tools → Board → Boards Manager*) zainstaluj pakiet **`esp32 by Espressif Systems`** w wersji **`3.3.5`** (biblioteka `NimBLE-Arduino 2.3.7` jest skompilowana pod strukturę kontrolera BT z ESP-IDF v5.5 w wersji `3.3.5`).
3. W menedżerze bibliotek (*Tools → Manage Libraries*) zainstaluj:
   - **`NimBLE-Arduino`** w wersji **`2.3.7`**,
   - **`LovyanGFX`** w wersji **`1.2.19`**.
4. Otwórz szkic [`firmware/SmartFan/SmartFan.ino`](firmware/SmartFan/SmartFan.ino).
5. Wybierz płytkę **`ESP32S3 Dev Module`** (*Flash Size: 8MB*, *Partition Scheme: Default 8MB with spiffs*, *PSRAM: Disabled*) i kliknij **Upload (`→`)**.
6. W pliku [`firmware/SmartFan/src/Board.h`](firmware/SmartFan/src/Board.h) domyślnie włączony jest profil sprzętowy `#define FAN_AC_HARDWARE 1`. Do prób samego interfejsu na biurku bez podłączonego modułu AC możesz przestawić tę flagę na `0` (tryb `DRY-RUN`).

---

## Obsługa na co dzień

### 1. Panel w telefonie (iOS / Android)
1. Połącz telefon z siecią Wi-Fi **`SmartFan`** (sieć otwarta, działa lokalnie bez dostępu do internetu).
2. Panel otworzy się automatycznie lub po wpisaniu w przeglądarce **`http://192.168.4.1`**.
3. Aby sparować pas tętna: załóż pas, kliknij **„🔍 Skanuj pasy HR w pobliżu”**, a gdy pojawi się na liście — kliknij **„Połącz”**. Sterownik zapisze adres pasa w pamięci NVS i przy kolejnych treningach połączy się z nim automatycznie.
4. Wybierz tryb **`AUTO (HR)`** (sterowanie z pasa tętna) lub **`MANUAL`** (sterowanie suwakiem).

### 2. Fizyczne przyciski na obudowie

| Tryb aktualny | Krótkie `PAIR` (`GPIO 12`) | Przytrzymanie `PAIR` (`1,2 s`) | Krótkie `CONFIG` (`GPIO 16`) | Przytrzymanie `CONFIG` (`1,2 s`) |
|---|---|---|---|---|
| **OFF** | Włącz **AUTO** | **Wybór pasa HR** (skan BLE) | Włącz **MANUAL** | Otwórz **KONFIGURACJĘ BLE** |
| **AUTO** | **OFF** (natychmiast) | **OFF** | — | — |
| **MANUAL** | **OFF** (natychmiast) | **OFF** | Cel **+5%** | Cel **−5%** |
| **WYBÓR PASA** | Następny pas na liście | Wyjdź do **OFF** | Zatwierdź wybrany pas | Wyjdź do **OFF** |
| **KONFIGURACJA** | Wyjdź do **OFF** | Wyjdź do **OFF** | — | **Usuń sparowane urządzenia BLE** |

### 3. Domyślne strefy tętna (tryb AUTO)

| Strefa | Zakres tętna | Nastawa wentylatora |
|---|---|---|
| Poniżej Z1 | `< 105 BPM` | `0%` (wyłączony) |
| **Z1** | `105–124 BPM` | `30%` |
| **Z2** | `125–144 BPM` | `45%` |
| **Z3** | `145–164 BPM` | `60%` |
| **Z4** | `165–199 BPM` | `75%` |
| **Z5** | `≥ 200 BPM` | `95%` |

*(Wszystkie progi BPM, moce `%`, histerezę `3 BPM`, filtr `2000 ms` oraz prędkości rampy możesz w każdej chwili zmienić w panelu `http://192.168.4.1` lub przez skrypt CLI na Windows).*

---

## Konfigurator Windows CLI (opcjonalny)

Oprócz panelu Wi-Fi projekt zawiera klienta linii poleceń BLE dla systemu Windows ([`client/smartfan.py`](client/smartfan.py)) z parowaniem kodem PIN i weryfikacją CRC-32:

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

## Budowanie z wiersza poleceń i testy automatyczne

Projekt posiada zestaw testów jednostkowych i integracyjnych C++20 (kompilowanych na hoście z użyciem **UBSan** oraz **AddressSanitizer**), testy klienta Python oraz skrypt statycznej inspekcji symboli `IRAM`/`DRAM` w pliku ELF (`Inspect-Isr.ps1`):

```powershell
.\scripts\Prepare-Firmware.ps1
.\scripts\Prepare-Host.ps1
.\scripts\Test.ps1
.\scripts\Test.ps1 -AddressSanitizer
.\scripts\Build.ps1 -Profile both
.\scripts\Inspect-Isr.ps1
.\scripts\Package.ps1
```

Szczegółowa dokumentacja techniczna znajduje się w katalogu [`docs/`](docs/):
- [Architektura systemu i współbieżność](docs/ARCHITEKTURA.md)
- [Raport weryfikacji i powtarzalności kompilacji](docs/RAPORT_WERYFIKACJI.md)
- [Macierz wymagań i testów (T01–T22, R01–R06)](docs/MACIERZ_WYMAGAN_I_TESTOW.md)
- [Sprzęt, profil fazowy i procedury odbiorcze](docs/SPRZET_I_ODBIOR.md)
- [Specyfikacja protokołu BLE GATT](docs/PROTOKOL_BLE.md)

---

## Bezpieczeństwo i informacje prawne

### Napięcie sieciowe 230 V AC
W wariancie sprzętowym (`ac-hardware`) sterownik współpracuje z regulatorem fazowym i wentylatorem zasilanym z sieci 220–240 V AC, 50 Hz. Wszelkie prace przy obwodach 230 V AC należy wykonywać przy odłączonym zasilaniu, z zachowaniem izolacji galwanicznej oraz zabezpieczenia nadprądowego (bezpiecznik topikowy w obwodzie zasilania wentylatora).

### Wyłączenie odpowiedzialności (AS IS)
Ten projekt stworzyłem w celach edukacyjnych i hobbystycznych. Urządzenie nie jest certyfikowanym wyrobem medycznym ani certyfikowanym sprzętem elektrycznym. Cały kod źródłowy oraz dokumentację udostępniam w stanie takim, w jakim się znajdują ("AS IS"), bez jakichkolwiek gwarancji. Nie ponoszę odpowiedzialności za szkody wynikające z budowy, modyfikowania lub użytkowania tego układu.

### Prawa autorskie i wykorzystane biblioteki open-source
Kod źródłowy oraz dokumentacja projektu HRM SmartFan v2 stanowią moją własność intelektualną. W projekcie wykorzystałem następujące biblioteki open-source, do których prawa posiadają ich twórcy:
- `Arduino-ESP32` (Espressif Systems) – licencja LGPL-2.1,
- `ESP-IDF` (Espressif Systems) – licencja Apache-2.0,
- `NimBLE-Arduino` (h2zero / Apache Mynewt) – licencja Apache-2.0,
- `LovyanGFX` (lovyan03) – licencja BSD-2-Clause,
- `Bleak` oraz pakiety `winrt-*` (PyWinRT) – licencja MIT.

### Znaki towarowe
Wymienione w dokumentacji nazwy handlowe i znaki towarowe (w tym Bluetooth, Windows, ESP32-S3, Garmin, Polar, Wahoo, RobotDyn, Android, iOS) należą do ich odpowiednich właścicieli i użyłem ich wyłącznie informacyjnie.
