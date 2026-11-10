# Macierz wymagań i testów

Testy hostowe korzystają z kodu produkcyjnego C++. Testy klienta używają mocka radia i porównują wektor ramki z kompilacją C++. Wyniki i zakres ograniczeń zawiera [raport weryfikacji](RAPORT_WERYFIKACJI.md).

| ID | Weryfikacja | Wynik i rodzaj dowodu |
|---|---|---|
| T01 | `bootAndModes`, walidacja rekordu | PASS host. Brak konfiguracji, profil niezatwierdzony, restart w OFF |
| T02 | `parser` | PASS host. 8/16 bit, energia, wiele RR, obcięcie |
| T03 | `parser`, `freshness` | PASS host. Obecny, nieobecny i nieznany kontakt |
| T04 | `freshness` | PASS host. Timeout dokładnie po 5000 ms, nieprawidłowa próbka go nie odświeża |
| T05 | `freshness`, `Radio::attach/detach` | PASS host i przegląd. Stara generacja jest odrzucana, nowe dane wznowione bez zmiany trybu |
| T06 | `lifecycleModel`, pojedyncze `createClient` | PASS model. 1100 nieudanych prób, szczyt 1 klient, backoff 1–30 s |
| T07 | `rampAndBounds` | PASS host. Minimum 30 i cel 30 → 0 |
| T08 | `rampAndBounds` | PASS host. Każdy próg, 200/201 i 225/226, monotoniczność 25–250 BPM |
| T09 | `protocol`, `test_strict_import_and_export` | PASS host/klient. Progi bez powtórzeń, nakładania i zakresów jawnych z lukami, błędne typy/zakresy odrzucone |
| T10 | `rampAndBounds` | PASS host. Limit 50, OFF podczas rozruchu |
| T11 | `bootAndModes` | PASS host. MANUAL bez pasa, AUTO oczekuje na dwie próbki |
| T12 | `bootAndModes`, `freshness` | PASS host. OFF pozostaje po reconnect |
| T13 | `rampAndBounds`, `freshness`, `phaseTiming` | PASS host. Zmienny krok, długa przerwa, rollover czasu |
| T14 | `phaseTiming` | PASS host. Filtr odstępu i brakujące Z-C, usterka przy przerwaniu pracy |
| T15 | `phaseTiming`, `Inspect-Isr.ps1` | PASS host i ELF. Anulowanie starego alarmu, wygaszenie zgody |
| T16 | `freshness`, `bootAndModes`, przegląd ścieżek inicjalizacji | PASS w modelu i przeglądzie. Brak zasobów powoduje blokadę, kolejka unieważnia wejście. Wstrzykiwanie awarii na płytce należy do H02 |
| T17 | `protocol`, `test_stream_resync`, `test_set_save_retry_close_get` | PASS host/mock. Fragmentacja, współdzielony zapis, timeout, CRC, duplikat |
| T18 | `protocol`, `test_unsuccessful_transactions` | PASS model transakcji. Brak fragmentu, odrzucenie SAVE, nowa sesja, uszkodzony rekord. Reset przy rzeczywistym flash należy do H01 |
| T19 | `bootAndModes`, przegląd `Radio::tick` | PASS host/przegląd. OFF w CONFIG i po wyjściu, zamknięcie linku bez niszczenia stosu |
| T20 | `protocol`, przegląd właściwości GATT | PASS host/przegląd. Klient bez uwierzytelnienia albo poza oknem nie wykonuje operacji |
| T21 | `tests/test_client.py`, `evidence/client-cli.log` | PASS mock i lokalny CLI. GET, SET, SAVE, eksport/import, rozłączenie i odczyt porównawczy |
| T22 | `Test.ps1`, `Build.ps1`, `Inspect-Isr.ps1`, `Package.ps1` | PASS build host i oba profile ESP32-S3. Sumy i identyfikatory w raporcie |
| H01 | `SPRZET_I_ODBIOR.md` | NOT RUN. Płytka, ekran, przyciski, flash, pas, konfigurator Windows |
| H02 | `SPRZET_I_ODBIOR.md` | NOT RUN. Z-C/PSM, latencja OFF, zakłócenia, zawieszenie sterowania |
| H03 | `SPRZET_I_ODBIOR.md` | NOT RUN. Napęd, rozruch, zatwierdzenie nastaw i praca długotrwała |
