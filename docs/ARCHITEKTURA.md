# Architektura HRM SmartFan v2

Zadanie sterowania zaprojektowałem jako jedynego właściciela trybu pracy, bieżącej próbki HR, rampy oraz wyjścia. Osobne zadania odpowiadają za operacje BLE/NVS oraz obsługę ekranu. Przerwania fazowe otrzymują wyłącznie krótkoterminową zgodę czasową i ograniczoną nastawę.

| Zdarzenie | Tryb po zdarzeniu | Wyjście |
|---|---|---|
| Boot/reset | OFF | 0 |
| Fizyczne OFF z dowolnego trybu | OFF | 0, anulowanie alarmu |
| Wejście/wyjście CONFIG albo wyboru pasa | CONFIG/WYBÓR -> OFF | 0 |
| Świadome AUTO | AUTO | 0 do dwóch świeżych próbek, potem krzywa HR |
| Świadome MANUAL | MANUAL | Rampa do bieżącej nastawy, także bez pasa |
| AUTO bez kontaktu, sesji lub świeżości | AUTO | 0, ponownie dwie świeże próbki |
| Reconnect podczas OFF lub MANUAL | Dotychczasowy | Bez zmiany przez sam reconnect |
| Błędna konfiguracja lub pusty profil | Wybrany tryb widoczny na ekranie | 0, komunikat blokady |
| Błąd inicjalizacji, utrata synchronizacji w pracy, wygaśnięcie zgody lub spóźniony impuls | OFF | 0. Usterkę fazową trzeba zdiagnozować przed restartem |

## Moduły i współbieżność

- Moduł `Core` zawiera walidację i kodowanie konfiguracji, parser HRS, maszynę stanów sterowania oraz logikę backoffu. W module `Phase` umieściłem filtrowanie Z-C, planowanie impulsów i obsługę zgody czasowej, a w `RadioLogic` maszynę stanów radia (rekomendacja z audytu: wybór pasa, ponawianie połączeń, przejścia do okna CONFIG i kontrola sesji za abstrakcją `RadioPort`). Wszystkie te moduły kompiluję bezpośrednio w testach hostowych. Warstwa `Driver` łączy `Phase` ze sprzętowymi rejestrami ESP32-S3 albo z wyjściem symulowanym, natomiast `Radio` jest cienkim adapterem NimBLE/FreeRTOS dla `RadioLogic`.
- Zadanie `ControlTask` przypięte do rdzenia 1 jest jedynym właścicielem trybu i nastawy. Odczytuje przyciski, pobiera zdarzenia HR i publikuje migawkę stanu (`Snapshot`). Fizyczne wymuszenie trybu OFF zeruje sterownik synchronicznie po odfiltrowaniu drgań styków. Po starcie płytki to zadanie nie wykonuje żadnego zapisu do NVS, skanu BLE ani odświeżania ekranu.
- Moduł `Radio` na rdzeniu 0 obsługuje pojedynczy, reużywany obiekt klienta HRS. Przed zmianą roli lub ponownym łączeniem zamyka aktywny link i czeka na potwierdzony stan rozłączenia. Stos NimBLE oraz obiekty GATT pozostają w pamięci przez cały czas pracy urządzenia. Wejście w tryb CONFIG zamyka sesję HR bez niszczenia stosu, co zapobiega zwalnianiu wskaźników, z których mógłby jeszcze korzystać callback.
- Callback notyfikacji przypisałem do generacji subskrypcji. Kopiuje on ograniczony komunikat do kolejki, worker odrzuca obce generacje, a pętla sterowania niezależnie weryfikuje identyfikator sesji. W razie przepełnienia kolejki sterowanie opróżnia ją, unieważnia pomiar i w trybie AUTO wymaga odebrania dwóch kolejnych poprawnych próbek. Tryb MANUAL działa niezależnie od odczytów HR.
- `ConfigBle` przekazuje odebrane fragmenty z callbacku do zadania radia, które weryfikuje token połączenia, aktywne okno fizyczne, autoryzację i spójność ramki. Przepełnienie kolejki RX natychmiast zamyka sesję. Moduł `Storage` obsługuje pamięć NVS z poziomu tego samego zadania. Pełna konfiguracja trafia do zadania sterowania przez kolejkę o długości 1. Bieżący tryb, nastawa chwilowa ani impulsy nie są zapisywane w NVS.
- `Display` odczytuje kopię stanu co 150 ms. Bufor ekranu 240 x 240 przy głębi 8 bitów zajmuje około 56,25 KiB. Na ekranie stosuję polskie komunikaty zapisane w czystym ASCII.

## Krzywa, filtr i czasy

Nastawa bazowa poniżej pierwszego progu wynosi zero. Kolejne domyślne wartości to 30, 45, 60, 75 i 95 dla progów wynoszących odpowiednio 105, 125, 145, 165 i 200 BPM. Progi stanowią dolne granice stref, a ostatnia strefa obejmuje każdy wyższy, wiarygodny wynik. Histereza obniża próg wyjścia z aktualnej strefy o 3 BPM. Nastawa zerowa pomija rampę oraz minimalny próg wysterowania.

Za wiarygodne uznaję tętno w zakresie 25-250 BPM. Flagi kontaktu rozróżniają kontakt obecny, nieobecny oraz nieznany. Parser obsługuje pomiar 8- i 16-bitowy, waliduje opcjonalne pole energii oraz parzystą listę jednego lub więcej interwałów RR (choć samych wartości energii i RR nie wykorzystuję do sterowania). Niekompletne pola opcjonalne unieważniają całą ramkę, natomiast bity RFU we flagach są ignorowane zgodnie ze specyfikacją [Bluetooth SIG Heart Rate Service](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/HRS_v1.0/out/en/index-en.html).

Filtr wykładniczy bazuje na czasie odbioru próbki i stałej czasowej 2000 ms. Rampa korzysta z bieżącego czasu zadania z krokiem ograniczonym do 100 ms oraz domyślnym tempem wzrostu 15 pkt/s i spadku 8 pkt/s. Pierwsza niezerowa nastawa rusza od zatwierdzonego minimum albo z opcjonalnego profilu rozruchu. Timeout pomiaru wynosi 5 s od ostatniej poprawnej próbki, natomiast wykryty brak kontaktu na skórze jest obsługiwany natychmiast, bez czekania na timeout. Czasy względne w oprogramowaniu układowym liczę jako różnice liczników bez znaku, a timer fazy korzysta z 54-bitowego licznika sprzętowego od momentu startu.

Tryb wyboru pasa wyświetla do 12 kandydatów i nadaje priorytet urządzeniom reklamującym usługę HRS, ale uwzględnia także pasy bez reklamy UUID. Zapis wybranego pasa następuje dopiero po potwierdzeniu usługi `180D`, charakterystyki `2A37`, deskryptora CCCD `2902`, aktywnej subskrypcji oraz odebraniu pierwszej poprawnej próbki. Procedura reconnect korzysta z zapamiętanego adresu i jego typu z backoffem 1-30 s. Jeśli pas używa losowego adresu zmieniającego się w czasie, może wymagać ponownego ręcznego wyboru, ponieważ w wersji v2 nie rozwiązuję tożsamości pasa przez klucz IRK. Okno wyboru pasa trwa 60 s.

## Ścieżka fazowa

Sterownik fazowy ma wyłączną własność timera TG1/T0. Korzystam ze źródła taktowania XTAL 40 MHz z podziałem do 1 MHz. W środowisku Arduino ESP Timer używa układu SYSTIMER, a grupa timera nie jest resetowana podczas inicjalizacji. Uzyskanie gotowości wymaga trzech kolejnych odstępów Z-C mieszczących się w przedziale 9000-11000 us. Zbyt wczesne zbocze nie przesuwa punktu odniesienia i nie uzbraja impulsu, a zbocze spóźnione zeruje synchronizację. Jeżeli w chwili utraty synchronizacji zgoda na sterowanie była aktywna, układ zatrzaskuje usterkę. Prawidłowe zbocze w trakcie pracy planuje początek impulsu, a przerwanie alarmu planuje jego koniec jeszcze przed podniesieniem linii PSM. Opóźnienie wejścia do procedury ISR przekraczające 100 us na początku impulsu wyzwala blokadę bezpieczeństwa.

Współdzielony spinlock synchronizuje zadanie właściciela sterowania i obie procedury ISR. Przejście w tryb OFF w sekcji krytycznej natychmiast ustawia stan niski na GPIO, kasuje rejestr alarmu, czyści status przerwania i usuwa plan fazy. Zgoda czasowa wygasa po 30 ms bez odświeżenia przez pętlę sterowania. Timer końca impulsu dokańcza wygaszenie wyjścia nawet w razie zatrzymania głównego zadania. W profilu sprzętowym w obsłudze przerwań zastosowałem niskopoziomowe funkcje LL operujące bezpośrednio na rejestrach zamiast wysokopoziomowego sterownika GPTimer, ponieważ jego publiczne funkcje sterujące w przypiętej wersji SDK nie są umieszczone w pamięci IRAM. Skrypt kontroli pliku ELF weryfikuje sekcje kodu i danych oraz drzewo wywołań w ścieżce AC. Zobacz dokumentację: [Arduino-ESP32 3.3.5](https://github.com/espressif/arduino-esp32/releases/tag/3.3.5) oraz [ESP-IDF GPTimer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gptimer.html).

## Budżety do odbioru

| Element | Budżet lub ograniczenie |
|---|---|
| Sterowanie | Przerwa 5 ms między iteracjami. Docelowo 99% odstępów do 10 ms w normalnej pracy |
| Fizyczne OFF | Debounce 20 ms. Stanowiskowy cel do 35 ms od stabilnego naciśnięcia do ostatniego aktywnego PSM |
| Zawieszona pętla | Zgoda 30 ms. Model sprawdza odrzucenie nowych impulsów po jej wygaśnięciu |
| Stosy aplikacji | Przydział 4 KiB na sterowanie, 8 KiB na radio. Cel zapasu co najmniej 512 B w każdym zadaniu |
| RAM | Cel co najmniej 48 KiB wolnej sterty po inicjalizacji oraz podczas konfiguracji, z uwzględnieniem bufora ekranu |
| Pamięć flash | Docelowy rozmiar obrazu aplikacji do 1 MiB na profilu referencyjnym |
| Opóźnienie ISR | Nowy impuls dopuszczalny do 100 us po planowanej chwili. Rzeczywisty rozkład wymaga pomiarów |

Przerwanie GPIO skonfigurowałem na poziomie 1, a alarm timera na poziomie 2. Statyczna kontrola czasu i sekcji pamięci dotyczy skompilowanego obrazu, natomiast rzeczywiste pomiary latencji, jittera i zużycia pamięci dynamicznej wchodzą w zakres prób stanowiskowych H01-H02.
