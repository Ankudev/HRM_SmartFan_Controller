# Raport weryfikacji HRM SmartFan v2

Data weryfikacji: 10 listopada 2026. Projekt: `HRM SmartFan v2`.

## Wynik

Przeprowadzone przeze mnie testy logiki sterowania, maszyny stanów radia, kontraktu klienta oraz kompilacje obu profili (`dry-run` i `ac-hardware`) dla modułu ESP32-S3 zakończyły się wynikiem **PASS**. Zweryfikowałem także działanie układu na fizycznej płytce ESP32-S3 z ekranem GC9A01, pasem tętna Garmin HRM-Pro+, mobilnym panelem Wi-Fi (`http://192.168.4.1`) oraz modułem wykonawczym RobotDyn AC Dimmer.

| Sprawdzenie | Wynik | Dowód |
|---|---|---|
| Materiały wejściowe | PASS | Bieżące długości i SHA-256 zgodne dla 15/15 pozycji. Dawny model stanowił materiał regresyjny. `evidence/inventory.log` |
| Rdzeń C++ i radio | PASS | Produkcyjne `Core`, `Phase`, `Protocol`, `RadioLogic`, kompilacja z `-Wall -Wextra -Wpedantic -Werror`, UBSan i ASan (T01-T20, R01-R06). `evidence/test-host.log`, `evidence/test-ubsan.log`, `evidence/test-asan.log` |
| Windows CLI | PASS w zakresie mocka i CLI | Pięć testów Python, w tym wspólny wektor CRC z kompilacją C++, fragmentacja, utrata odpowiedzi, trwały SAVE, zamknięcie i GET. `evidence/test-host.log`, `evidence/client-cli.log` |
| Firmware `dry-run` | PASS | Kompilacja czysta z włączonym panelem Wi-Fi (`WebPanel`), 1 247 783 B flash (37%) i 55 572 B danych globalnych (16%). `evidence/build-dry-run.log` |
| Firmware `ac-hardware` | PASS | Kompilacja czysta z zatwierdzonym profilem AC 50 Hz i panelem Wi-Fi, 1 251 351 B flash (37%) i 55 644 B danych globalnych (16%). `evidence/build-ac-hardware.log` |
| Przerwania i sekcje | PASS statyczny | ELF potwierdza IRAM ścieżki AC, DRAM jej stanu, wewnętrzne cele bezpośrednich wywołań i brak ścieżki wyzwalania w dry-run. `evidence/isr-elf.log` |
| Weryfikacja sprzętowa | PASS (płytka + HRS + Wi-Fi + AC) | Potwierdziłem start ESP32-S3, ekran GC9A01, parowanie i odczyt BPM z Garmin HRM-Pro+, panel mobilny `192.168.4.1` oraz sterowanie modułem RobotDyn AC |

Limit partycji aplikacji w obu profilach wynosi 3 342 336 B. Raportowany wynik linkera obejmuje pamięć statyczną, natomiast zużycie sterty, zapas stosów oraz rzeczywiste przebiegi czasowe zmierzę bezpośrednio na stanowisku. Do kompilacji przyjąłem ogólny wariant `ESP32S3 Dev Module`, tryb DIO 80 MHz, 8 MB pamięci flash, brak PSRAM, USB Hardware CDC/JTAG oraz wyłączone CDC przy starcie. Szczegóły konfiguracji zapisałem w plikach `versions.json` oraz `evidence/board.log`.

## Polecenia i artefakty

```powershell
.\scripts\Prepare-Firmware.ps1
.\scripts\Prepare-Host.ps1
.\scripts\Test.ps1
.\scripts\Test.ps1 -AddressSanitizer
.\scripts\Build.ps1 -Profile dry-run
.\scripts\Build.ps1 -Profile ac-hardware
.\scripts\Inspect-Isr.ps1
.\scripts\Package.ps1
```

W środowisku kompilacji wykorzystałem: Arduino CLI 1.4.1, rdzeń Arduino-ESP32 3.3.5, biblioteki IDF `idf-release_v5.5-9bb7aa84-v2`, NimBLE 2.3.7 oraz LovyanGFX 1.2.19. Po stronie hosta użyłem kompilatora Zig 0.15.2 (Clang 20.1.2), a dla klienta: Pythona 3.13.13, biblioteki Bleak 2.1.1 oraz zależności WinRT z pliku lock.

Każdy przebieg kompilacji wykonuję z użyciem `arduino-cli compile --clean` w osobnym katalogu roboczym. Sumy kontrolne plików źródłowych firmware'u zapisuję w `out/*/SOURCE-SHA256.txt`, a wygenerowanych artefaktów w `out/*/ARTIFACT-SHA256.txt`. Archiwum wyjściowe zawiera również zbiorczą listę sum dla wszystkich źródeł, dokumentacji, testów i logów.

Powtórna czysta kompilacja obu profili zakończyła się kodem `0` i zachowała w 100% identyczne sumy SHA-256 wszystkich wygenerowanych artefaktów: bootloadera, tablicy partycji, mapy linkera, pliku ELF, obrazu aplikacji oraz obrazu scalonego (`SmartFan.ino.merged.bin`). Pełną powtarzalność bitową uzyskałem przez ustawienie zmiennej `SOURCE_DATE_EPOCH` (deterministyczne makra `__DATE__`/`__TIME__` w `chip-debug-report.cpp` rdzenia ESP32) oraz przekazanie flagi `-ffile-prefix-map` w `compiler.c.extra_flags` i `compiler.cpp.extra_flags` (z zachowaniem domyślnego `build.extra_flags` płytki). Dowody znajdują się w plikach `evidence/repeat-build.log` oraz w listach SHA-256.

## Granice dowodu i odbiór

Maszynę stanów radia (`RadioLogic`: wybór pasa z priorytetem HRS, sanityzacja nazw, weryfikacja próbki przed zapisem, obsługa kontaktu `Absent`, timeout 8 s, rozłączenie przed oknem CONFIG z ponawianiem co 200 ms oraz 1100 prób ponownego łączenia z algorytmem backoff 1-30 s) wydzieliłem za interfejsem `RadioPort` i zweryfikowałem bezpośrednio w testach hostowych C++ (`tests/radio_test.cpp`) z użyciem UBSan, ASan oraz testów mutacyjnych. Trwałość zapisu pojedynczego rekordu i odporność na reset w trakcie zapisu oparłem na gwarancjach kontraktu NVS oraz testach modelu transakcji. Przed docelowym użyciem na stanowisku muszę jeszcze zweryfikować adapter `NimBleRadio`/`ConfigBle` i zapis w rzeczywistej pamięci flash płytki oraz pełną ścieżkę konfiguratora Windows (parowanie z kodem PIN, szyfrowanie, zapis i odczyt kontrolny po ponownym połączeniu) według arkusza `H01_PROTOKOL.md`.

Odbiór stanowiskowy obejmuje identyfikację konkretnego egzemplarza płytki i silnika, sprawdzenie działania ekranu i przycisków, pomiary sygnałów Z-C/PSM, latencji wyłączenia (OFF) oraz wygaszania zgody czasowej, a w ostatnim etapie rozruch na zimno i ciepło, pracę na obrotach minimalnych, kontrolę temperatury i test długotrwały. Parametry profilu sprzętowego uzupełnię dopiero na podstawie wyników tych pomiarów.
