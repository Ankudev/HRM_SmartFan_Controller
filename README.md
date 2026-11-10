# HRM SmartFan v2

Autonomiczny sterownik nawiewu dla ESP32-S3, ekranu GC9A01 i pasa BLE z Heart Rate Service. Pracuje lokalnie. Komputer z Windows służy do konfiguracji.

Stan projektu i identyfikatory obrazów są w [raporcie weryfikacji](docs/RAPORT_WERYFIKACJI.md). Testy stanowiskowe H01–H03 mają status **NOT RUN**.

## Uruchomienie

1. Do pierwszej próby użyj profilu `dry-run`. Ekran oznacza go napisem DRY-RUN. GPIO18 nie jest konfigurowane przez ten profil.
2. Po starcie przytrzymaj CONFIG przez 1,2 s. Przy pierwszym użyciu sparuj urządzenie **SmartFan v2** w ustawieniach Bluetooth Windows. Wpisz jednorazowy PIN wyświetlony na ekranie sterownika.
3. Zapisz pełną konfigurację klientem Windows. Świeża instalacja pozostaje w OFF do udanego SAVE.
4. Zakończ CONFIG przyciskiem PAIR. Aby wybrać pas, przytrzymaj PAIR w OFF. Załóż i uaktywnij pas, poczekaj na listę, przewijaj PAIR i potwierdź CONFIG. Ekran wróci do OFF po odczycie poprawnej próbki i zapisie identyfikatora pasa.
5. Wybierz AUTO albo MANUAL przyciskiem. Wydanie AC wymaga uzupełnienia profilu konkretnego napędu po weryfikacji na stanowisku.

| Tryb | PAIR | CONFIG |
|---|---|---|
| OFF | Krótko AUTO, przytrzymanie wybór pasa | Krótko MANUAL, przytrzymanie CONFIG |
| AUTO | Naciśnięcie natychmiast wybiera OFF po filtracji drgań styku | — |
| MANUAL | Naciśnięcie wybiera OFF | Krótko +5 pkt, przytrzymanie −5 pkt |
| Wybór pasa | Krótko następny, przytrzymanie wyjście | Krótko wybierz, przytrzymanie wyjście |
| CONFIG | Naciśnięcie kończy okno | Przytrzymanie usuwa zaufane parowania i kończy okno |

Ustawienie chwilowej nastawy ręcznej na 0 wygasza nawiew. Zmiany z przycisku są ulotne. Ekran pokazuje aktualność tętna, tryb, nastawę i stan połączenia. `-- BPM` oznacza brak aktualnego pomiaru. Procent nastawy to pozycja w zakresie sterowania fazowego. Identyfikator obrazu i przyczyna resetu są dostępne również przez port szeregowy 115200 przy referencyjnej konfiguracji USB/UART.

## Konfigurator Windows

Uruchom PowerShell w katalogu projektu. Wersje narzędzi są w `versions.json`. Klient wymaga Windows x64 i Python 3.13.13.

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

Przy kilku sterownikach przekaż `--adres ADRES` przed podkomendą. Polecenia `set` i `import` wykonują SET, SAVE i porównują wynik GET po ponownym połączeniu. `defaults` i `export` tworzą nowy plik. Po usunięciu parowań ze sterownika usuń urządzenie również w ustawieniach Windows i sparuj od nowa. Przy zerwaniu połączenia w trakcie SAVE użyj GET, żeby sprawdzić, co zapisano.

## Build i testy

Wymagane PowerShell 7, Arduino CLI 1.4.1 dostępne w PATH albo jako `.local/arduino-cli.exe`, oraz Python. Przygotowanie firmware używa katalogów Arduino wewnątrz `.local`. Pobierane narzędzia zajmują kilka GB.

```powershell
.\scripts\Prepare-Firmware.ps1
.\scripts\Prepare-Host.ps1
.\scripts\Test.ps1
.\scripts\Build.ps1
.\scripts\Inspect-Isr.ps1
.\scripts\Package.ps1
```

Obrazy aplikacji, ELF, mapy linkera i sumy kontrolne są w `out/dry-run` oraz `out/ac-hardware`. Build używa referencyjnego ESP32-S3 z 8 MB flash i wyłączoną PSRAM. Wariant płytki fizycznej wymaga potwierdzenia według [procedury stanowiskowej](docs/SPRZET_I_ODBIOR.md). Konfigurator używa nowego [protokołu BLE](docs/PROTOKOL_BLE.md). Stara aplikacja Android nie ma obsługi tego protokołu.
