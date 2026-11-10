# Raport weryfikacji HRM SmartFan v2

Data weryfikacji: 6 listopada 2026. Projekt: `D:\APLIKACJE\HRM_SmartFan_v2`.

## Wynik

Testy logiki, kontraktu klienta i kompilacje obu profili dla referencyjnego ESP32-S3 uzyskały PASS. Testy fizyczne H01–H03 mają status **NOT RUN**. Obraz `ac-hardware` zawiera rzeczywisty sterownik fazowy i ma zablokowane impulsy z powodu pustego profilu napędu.

| Sprawdzenie | Wynik | Dowód |
|---|---|---|
| Materiały wejściowe | PASS | Bieżące długości i SHA-256 zgodne dla 15/15 pozycji. Dawny model stanowił materiał regresyjny. `evidence/inventory.log` |
| Rdzeń C++ | PASS | Produkcyjne `Core`, `Phase`, `Protocol`, kompilacja z `-Wall -Wextra -Wpedantic -Werror`, UBSan i ASan. `evidence/test-host.log`, `evidence/test-ubsan.log` |
| Windows CLI | PASS w zakresie mocka i CLI | Pięć testów Python, w tym wspólny wektor CRC z kompilacją C++, fragmentacja, utrata odpowiedzi, trwały SAVE, zamknięcie i GET. `evidence/test-host.log`, `evidence/client-cli.log` |
| Firmware `dry-run` | PASS | Identyfikator `fbb62f30dcf-0`, 727 575 B flash i 33 908 B danych globalnych |
| Firmware `ac-hardware` | PASS | Identyfikator `fbb62f30dcf-1`, 731 143 B flash i 33 972 B danych globalnych |
| Przerwania i sekcje | PASS statyczny | ELF potwierdza IRAM ścieżki AC, DRAM jej stanu, wewnętrzne cele bezpośrednich wywołań i brak ścieżki wyzwalania w dry-run. `evidence/isr-elf.log` |
| H01–H03 | NOT RUN | Procedury w `SPRZET_I_ODBIOR.md` |

Limit partycji aplikacji w obu profilach wynosi 3 342 336 B. Wynik linkera obejmuje pamięć statyczną. Zużycie sterty, zapas stosów i czasy są do pomiaru na stanowisku. Przyjęto ogólny wariant `ESP32S3 Dev Module`, DIO 80 MHz, 8 MB flash, bez PSRAM, USB Hardware CDC/JTAG i CDC przy starcie wyłączone. Szczegóły w `versions.json` i `evidence/board.log`.

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

Arduino CLI 1.4.1, Arduino-ESP32 3.3.5, biblioteki IDF `idf-release_v5.5-9bb7aa84-v2`, NimBLE 2.3.7 i LovyanGFX 1.2.19. Host: Zig 0.15.2 z Clang 20.1.2. Klient: Python 3.13.13, Bleak 2.1.1 i zależności WinRT z pliku lock.

Każdy build używa `arduino-cli compile --clean` i oddzielnego katalogu. Sumy plików źródłowych firmware są w `out/*/SOURCE-SHA256.txt`, artefaktów w `out/*/ARTIFACT-SHA256.txt`. Pakiet zawiera także zbiorczą listę źródeł, dokumentacji, testów i logów.

Powtórna czysta kompilacja obu profili zakończyła się kodem 0 i zachowała identyfikatory oraz rozmiary sekcji. W obu profilach SHA-256 bootloadera, partycji i mapy linkera były identyczne. Sumy ELF, obrazu aplikacji i obrazu scalonego różniły się między przebiegami. Esptool osadza skrót pliku ELF w obrazie aplikacji, więc zmiana ELF wpływa także na obraz aplikacji i obraz scalony. Pole Compile time pochodzi z prekompilowanego SDK Arduino i wskazuje datę kompilacji tego SDK. Dokładnego źródła różnicy bajtów między ELF nie ustalono. Identyfikator treści źródeł pozostał taki sam, a publikowane sumy identyfikują konkretne spakowane obrazy. Dowody są w `evidence/repeat-build.log`, `evidence/image-info-*.log` i listach SHA-256.


## Granice dowodu i odbiór

Reużycie klienta i backoff sprawdzono w modelu 1100 nieudanych prób oraz w kodzie adaptera NimBLE. Trwałość pojedynczego rekordu i reset w trakcie zapisu są oparte na kontrakcie NVS i testach modelu transakcji. Przed użyciem na stanowisku należy sprawdzić rzeczywisty flash oraz przejście konfiguratora Windows przez parowanie PIN, szyfrowanie, zapis i odczyt po ponownym połączeniu.

Odbiór stanowiskowy obejmuje identyfikację płytki i konkretnego silnika, sprawdzenie ekranu i przycisków, pomiary Z-C/PSM oraz latencji OFF i dzierżawy, a następnie rozruch na zimno i ciepło, minimum, temperaturę i próbę długą. Wartości profilu sprzętowego należy uzupełnić na podstawie tych wyników.
