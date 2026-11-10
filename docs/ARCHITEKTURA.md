# Architektura HRM SmartFan v2

Sterowanie jest właścicielem trybu, bieżącej próbki HR, rampy i wyjścia. Osobne zadania wykonują operacje BLE/NVS oraz obsługę ekranu. Przerwania fazowe dostają wyłącznie krótkoterminową zgodę i ograniczoną nastawę.

| Zdarzenie | Tryb po zdarzeniu | Wyjście |
|---|---|---|
| Boot/reset | OFF | 0 |
| Fizyczne OFF z dowolnego trybu | OFF | 0, anulowanie alarmu |
| Wejście/wyjście CONFIG albo wyboru pasa | CONFIG/WYBÓR → OFF | 0 |
| Świadome AUTO | AUTO | 0 do dwóch świeżych próbek, potem krzywa HR |
| Świadome MANUAL | MANUAL | Rampa do bieżącej nastawy, także bez pasa |
| AUTO bez kontaktu, sesji lub świeżości | AUTO | 0, ponownie dwie świeże próbki |
| Reconnect podczas OFF lub MANUAL | Dotychczasowy | Bez zmiany przez sam reconnect |
| Błędna konfiguracja lub pusty profil | Wybrany tryb widoczny na ekranie | 0, komunikat blokady |
| Błąd inicjalizacji, utrata synchronizacji w pracy, wygaśnięcie zgody lub spóźniony impuls | OFF | 0. Usterkę fazową trzeba zdiagnozować przed restartem |

## Moduły i współbieżność

- `Core` zawiera walidację i kodowanie konfiguracji, parser HRS, stan sterowania i backoff. `Phase` zawiera filtrowanie Z-C, planowanie i zgodę czasową. Oba moduły są kompilowane też w testach hostowych. `Driver` łączy Phase z rejestrami ESP32-S3 albo z wyjściem symulowanym.
- `ControlTask` na rdzeniu 1 jest jedynym właścicielem trybu i nastawy. Sprawdza przyciski, pobiera zdarzenia HR i publikuje snapshot. Fizyczne OFF zeruje sterownik synchronicznie po odfiltrowaniu styku. Po uruchomieniu płytki żaden zapis NVS, skan BLE ani odświeżenie ekranu nie jest wykonywane przez to zadanie.
- `Radio` na rdzeniu 0 obsługuje jeden reużywany obiekt klienta HRS. Zamyka link i czeka na potwierdzony stan rozłączenia przed zmianą roli lub ponownym łączeniem. Stos NimBLE oraz obiekty GATT żyją przez cały czas działania. Przejście CONFIG zamyka sesję HR bez niszczenia stosu. Zapobiega to uwalnianiu wskaźników, z których mógłby jeszcze korzystać callback.
- Callback notyfikacji jest przypisany do generacji subskrypcji. Kopiuje ograniczony komunikat do kolejki. Worker odrzuca obce generacje, a sterowanie niezależnie sprawdza identyfikator sesji. Po przepełnieniu sterowanie opróżnia kolejkę, unieważnia pomiar i w AUTO wymaga dwóch kolejnych poprawnych próbek. MANUAL działa bez HR.
- `ConfigBle` przekazuje fragmenty z callbacku do zadania radia, które sprawdza token połączenia, okno fizyczne, autoryzację i spójność ramki. Przepełnienie kolejki RX zamyka sesję. `Storage` obsługuje NVS z tego samego zadania. Pełna konfiguracja wchodzi do sterowania przez kolejkę o długości 1. Tryb, nastawa chwilowa i impulsy nie trafiają do NVS.
- `Display` odczytuje kopię stanu co 150 ms. Bufor 240 × 240 przy głębi 8 bit zajmuje około 56,25 KiB. Ekran używa polskich komunikatów w zapisie ASCII.

## Krzywa, filtr i czasy

Nastawa bazowa poniżej pierwszego progu to zero. Kolejne wartości domyślne to 30, 45, 60, 75 i 95 od progów odpowiednio 105, 125, 145, 165 i 200 BPM. Progi są dolnymi granicami. Ostatnia strefa obejmuje każdy wyższy wiarygodny wynik. Histereza obniża próg wyjścia z aktualnej strefy o 3 BPM. Zero pomija rampę i minimalną nastawę.

Wiarygodny HR mieści się w zakresie 25–250 BPM. Flagi kontaktu rozróżniają obecny, nieobecny i nieznany kontakt. Parser dopuszcza pomiar 8/16 bit, waliduje opcjonalną energię i parzystą listę jednego lub więcej RR. Nie wykorzystuje energii ani RR do sterowania. Niekompletne opcjonalne pola unieważniają ramkę. Wartości RFU flag są ignorowane. [Bluetooth SIG Heart Rate Service](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/HRS_v1.0/out/en/index-en.html).

Filtr wykładniczy używa czasu odbioru próbki i stałej 2000 ms. Rampa używa aktualnego czasu zadania, z krokiem ograniczonym do 100 ms i domyślnym wzrostem 15 pkt/s oraz spadkiem 8 pkt/s. Pierwsza niezerowa nastawa startuje od zatwierdzonego minimum albo z opcjonalnego profilu rozruchu. Timeout wynosi 5 s od ostatniej ważnej próbki. Brak kontaktu jest przetwarzany bez czekania na timeout. Czasy względne firmware są różnicami liczników bez znaku, a timer fazy korzysta z 54-bitowego licznika sprzętowego od bootu.

Wybór pasa pokazuje do 12 kandydatów i priorytetyzuje reklamowaną usługę HRS. Uwzględnia też urządzenia bez reklamy UUID. Zapis następuje po zgodnej usłudze 180D, charakterystyce 2A37, CCCD 2902, potwierdzonej subskrypcji i ważnej próbce. Reconnect używa zapamiętanego adresu i typu z backoffem 1–30 s. Losowy adres zmieniający się w czasie może wymagać ponownego świadomego wyboru. Wersja v2 nie rozwiązuje tożsamości pasa za pomocą IRK. Okno wyboru trwa 60 s.

## Ścieżka fazowa

Sterownik ma wyłączną własność TG1/T0. Używa XTAL 40 MHz z podziałem do 1 MHz. Arduino ESP Timer korzysta z SYSTIMER, a grupa timera nie jest resetowana podczas inicjalizacji. Gotowość wymaga trzech kolejnych odstępów 9000–11000 µs. Wcześniejsze zbocze nie zmienia punktu odniesienia i nie uzbraja impulsu. Spóźnione zbocze zeruje synchronizację. Jeżeli zgoda na sterowanie była aktywna, utrata synchronizacji zatrzaskuje usterkę. Rozpoznane zbocze w trakcie pracy planuje początek impulsu, a alarm planuje jego koniec, zanim podniesie PSM. Opóźnienie ISR powyżej 100 µs na początku impulsu wyzwala blokadę.

Jeden spinlock synchronizuje właściciela sterowania i oba ISR. OFF obniża GPIO, kasuje rejestr alarmu, czyści status przerwania i usuwa plan fazy w sekcji krytycznej. Zgoda wygasa po 30 ms bez aktualizacji. Timer końca impulsu działa również po zatrzymaniu zadania. W profilu sprzętowym obsługa przerwań używa funkcji LL operujących na rejestrach, zamiast sterownika GPTimer, którego publiczne funkcje sterujące nie mają w przypiętym SDK włączonej opcji IRAM. Kontrola ELF dokumentuje sekcje kodu i danych oraz wywołania w ścieżce AC. [Arduino-ESP32 3.3.5](https://github.com/espressif/arduino-esp32/releases/tag/3.3.5), [ESP-IDF GPTimer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gptimer.html).

## Budżety do odbioru

| Element | Budżet lub ograniczenie |
|---|---|
| Sterowanie | Przerwa 5 ms między iteracjami. Docelowo 99% odstępów ≤10 ms w normalnej pracy |
| Fizyczne OFF | Debounce 20 ms. Stanowiskowy cel ≤35 ms od stabilnego naciśnięcia do ostatniego aktywnego PSM |
| Zawieszona pętla | Zgoda 30 ms. Model sprawdza odrzucenie nowych impulsów po jej wygaśnięciu |
| Stosy aplikacji | Przydział 4 KiB sterowanie, 8 KiB radio. Cel zapasu ≥512 B na każdym zadaniu |
| RAM | Cel ≥48 KiB wolnego sterty po inicjalizacji oraz podczas konfiguracji, z uwzględnieniem bufora ekranu |
| Pamięć flash | Cel obraz aplikacji ≤1 MiB na profilu referencyjnym |
| Opóźnienie ISR | Nowy impuls dopuszczalny do 100 µs po planowanej chwili. Rzeczywisty rozkład wymaga pomiarów |

Przerwanie GPIO jest na poziomie 1, alarm timera na poziomie 2. Kontrola czasu i sekcji pamięci dotyczy obrazu, a pomiary latencji, jittera i pamięci dynamicznej są częścią H01–H02.
