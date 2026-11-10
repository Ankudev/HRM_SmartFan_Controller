# Protokół H01: sterownik bez części AC

Arkusz do wypełnienia przy płytce. Rozwija punkt H01 z [SPRZET_I_ODBIOR.md](SPRZET_I_ODBIOR.md). Każdy wiersz ma oczekiwany wynik wynikający z kodu. Wynik inny niż oczekiwany oznacza FAIL, nawet jeśli wygląda na nieszkodliwy.

> [!CAUTION]
> H01 wykonuje się wyłącznie na obrazie `dry-run`, bez modułu Z-C/PSM i bez napięcia 230 V w pobliżu stanowiska. Obraz `dry-run` nie konfiguruje GPIO18 jako wyjścia wyzwalania.

## Dane przebiegu

| Pole | Wartość |
|---|---|
| Data, osoba | |
| Płytka (oznaczenie, wariant flash/PSRAM) | |
| Identyfikator obrazu (ekran, linia `HRM SmartFan v2 ...` na porcie szeregowym) | |
| `out/dry-run/ARTIFACT-SHA256.txt` (suma `SmartFan.ino.bin`) | |
| Pas(y) HR: model, wersja firmware, typ adresu (publiczny / losowy) | |
| Komputer z Windows: wersja systemu, adapter BLE | |
| Python i Bleak (`.local/venv`) | |

## Przygotowanie

1. `.\scripts\Build.ps1 -Profile dry-run`, potem wgranie `out/dry-run/build/SmartFan.ino.merged.bin` od adresu `0x0`.
2. Monitor szeregowy 115200 baud. Przy `CDCOnBoot=default` obiekt `Serial` działa na UART0 (GPIO43/44), więc na DevKitC-1 trzeba użyć złącza **UART**, a nie natywnego USB.
3. Przed pierwszym uruchomieniem wyczyścić NVS (`esptool erase_flash` przed wgraniem), żeby sprawdzić start z pustą konfiguracją.
4. Co 5 s firmware wypisuje linię `diag`: wolną stertę, minimum sterty, zapas stosów zadań (w bajtach) i stan radia. Te wartości zapisuje się w punkcie 5.

| Pole `diag` | Kody |
|---|---|
| `mode` | 0 OFF, 1 AUTO, 2 MANUAL, 3 PAIR, 4 CONFIG |
| `reason` | 0 Off, 1 Running, 2 Waiting, 3 Stale, 4 NoContact, 5 NoLink, 6 Profile, 7 InvalidConfig, 8 Sync, 9 Fault, 10 Pair, 11 Config |
| `radio` | 0 Boot, 1 Idle, 2 Connecting, 3 Subscribed, 4 Scanning, 5 Choose, 6 Checking, 7 NoCandidates, 8 PairFailed, 9 PairSaved, 10 Config, 11 RadioError, 12 BondsRemoved, 13 BondError |
| `attempts` | Liczba prób `connect` od startu |

## 1. Płytka i start

| # | Czynność | Oczekiwany wynik | Wynik |
|---|---|---|---|
| 1.1 | Zasilenie z USB | Na porcie: `HRM SmartFan v2 <id> AC=0 reset=1`. Brak linii `Blad ...` | |
| 1.2 | Ekran | Tryb `OFF`, przyczyna `Ustaw konfiguracje` (pusty NVS), na dole `DRY-RUN <id>` | |
| 1.3 | Start z wciśniętym PAIR, puszczenie po 3 s | Brak zmiany trybu. Przyciski są ignorowane do pierwszego puszczenia obu | |
| 1.4 | LED (GPIO13) | Zgaszona w OFF | |

## 2. Przyciski i tryby (bez pasa, po zapisie konfiguracji z punktu 4)

| # | Czynność | Oczekiwany wynik | Wynik |
|---|---|---|---|
| 2.1 | OFF, krótkie CONFIG | `MANUAL`, nastawa rośnie rampą do 45% (domyślna nastawa ręczna), LED świeci | |
| 2.2 | MANUAL, krótkie CONFIG / przytrzymanie CONFIG (≥ 1,2 s) | Cel +5 / −5 punktów | |
| 2.3 | MANUAL, wciśnięcie PAIR | Natychmiast `OFF`, nastawa 0, LED gaśnie | |
| 2.4 | OFF, krótkie PAIR bez pasa | `AUTO`, przyczyna `Brak polaczenia`, nastawa 0 | |
| 2.5 | Restart w trakcie MANUAL | Po restarcie `OFF`. Tryb nie jest odtwarzany | |
| 2.6 | Restart po zapisie konfiguracji | Konfiguracja zachowana, brak `Ustaw konfiguracje` | |

## 3. Pas HR

| # | Czynność | Oczekiwany wynik | Wynik |
|---|---|---|---|
| 3.1 | OFF, przytrzymanie PAIR | `WYBOR PASA`, `Szukam urzadzen` przez ok. 2,5 s, potem lista `1/N` | |
| 3.2 | Kilka pasów w zasięgu, krótkie PAIR | Kolejny kandydat. Najpierw urządzenia ogłaszające usługę HR | |
| 3.3 | Krótkie CONFIG na właściwym pasie (założonym) | `Sprawdzam pas`, potem `Zapisano pas` i powrót do OFF | |
| 3.4 | Jak 3.3, ale pas niezałożony | `Nie zweryfikowano` po maks. 8 s. Nic nie zostaje zapisane | |
| 3.5 | Brak decyzji przez 60 s | Powrót do OFF | |
| 3.6 | OFF z zapisanym pasem | Samoczynne połączenie, `Pas polaczony`, BPM na ekranie | |
| 3.7 | AUTO, pas założony | `Czekam na 2 probki`, potem `Sterowanie` | |
| 3.8 | AUTO, zdjęcie pasa ze skóry | `Brak kontaktu pasa` (jeśli pas raportuje kontakt) albo `Brak swiezego HR`, nastawa 0 | |
| 3.9 | AUTO, wyłączenie / oddalenie pasa | Po 5 s `Brak swiezego HR`, nastawa 0. Tryb pozostaje AUTO | |
| 3.10 | Powrót pasa | Ponowne połączenie (backoff 1–30 s), dwie świeże próbki, wznowienie bez zmiany trybu | |
| 3.11 | MANUAL, odłączenie pasa | Nastawa ręczna bez zmian | |
| 3.12 | Pas z adresem losowym (jeśli dostępny): zdjęcie na > 15 min, ponowne założenie | Zanotować, czy łączy się bez ponownego wyboru. Brak IRK może wymagać ponownego parowania | |

## 4. Konfigurator Windows

| # | Czynność | Oczekiwany wynik | Wynik |
|---|---|---|---|
| 4.1 | OFF, przytrzymanie CONFIG | `KONFIGURACJA`, `BLE: SmartFan v2`, ogłaszanie | |
| 4.2 | `smartfan.py` GET, parowanie | Na ekranie 6-cyfrowy PIN, Windows prosi o PIN, po wpisaniu odpowiedź `Empty` (pusty NVS) | |
| 4.3 | Błędny PIN | Parowanie odrzucone, brak odpowiedzi na ramki | |
| 4.4 | Inny, niesparowany klient (np. nRF Connect) zapisuje do RX | Rozłączenie albo brak odpowiedzi | |
| 4.5 | SET + SAVE ustawień domyślnych, potem GET po ponownym połączeniu | Odczyt zgodny bit w bit, klient zgłasza sukces weryfikacji | |
| 4.6 | Wciśnięcie PAIR w trakcie sesji | Natychmiast `OFF`, połączenie zerwane | |
| 4.7 | Brak aktywności 180 s | Okno wygasa, `OFF` | |
| 4.8 | Przytrzymanie CONFIG w trybie KONFIGURACJA | `Usunieto parowania`, `OFF`. Kolejne połączenie wymaga nowego PIN-u | |
| 4.9 | Opcjonalnie ATT MTU 23 | Fragmentacja po 20 B działa, wynik jak 4.5 | |

## 5. Zasoby i odporność

Wartości z linii `diag` (minimum z całego przebiegu):

| Scenariusz | Min. sterta [B] | Zapas stosu Sterowanie [B] | Zapas stosu BLE_NVS [B] | Zapas stosu loop [B] |
|---|---|---|---|---|
| Bezczynny OFF | | | | |
| Skanowanie (3.1) | | | | |
| Połączenie z pasem (3.6) | | | | |
| Sesja konfiguracji, SAVE (4.5) | | | | |

Kryterium (zgodnie z [ARCHITEKTURA.md](ARCHITEKTURA.md)): zapas każdego stosu ≥ 512 B, wolna sterta ≥ 48 KiB (49 152 B). Przy niższym zapasie trzeba zwiększyć rozmiar zadania w `SmartFan.ino` i powtórzyć pomiar.

| # | Czynność | Oczekiwany wynik | Wynik |
|---|---|---|---|
| 5.1 | Odłączenie zasilania w trakcie SAVE (kilka prób) | Po starcie poprzednia albo nowa konfiguracja, nigdy uszkodzona. Przy uszkodzonym rekordzie: `Ustaw konfiguracje` | |
| 5.2 | Zerwanie łącza BLE w trakcie SAVE | Klient zgłasza błąd albo ponawia. GET pokazuje spójny stan | |
| 5.3 | Pas w zasięgu przez 30 min | Licznik `attempts` w `diag` nie rośnie bez potrzeby, sterta stabilna | |

## Wynik

| Sekcja | PASS / FAIL | Uwagi |
|---|---|---|
| 1 Płytka i start | | |
| 2 Przyciski i tryby | | |
| 3 Pas HR | | |
| 4 Konfigurator | | |
| 5 Zasoby | | |

Log z portu szeregowego zapisuje się jako `evidence/h01-serial.log`, a wypełniony arkusz dołącza do raportu weryfikacji.

