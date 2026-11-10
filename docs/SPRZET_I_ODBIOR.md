# Sprzęt i odbiór

Układ docelowy obejmuje moduł ESP32-S3-WROOM-1, okrągły wyświetlacz GC9A01 240 x 240 SPI, pas tętna Garmin HRM-Pro Plus (lub inny zgodny ze standardem HRS), regulator fazowy RobotDyn Z-C/PSM oraz wentylator sieciowy 220-240 V, około 60 W, 50 Hz.

| Sygnał | GPIO |
|---|---:|
| TFT SCK / MOSI | 10 / 11 |
| TFT CS / DC / RESET / BL | 9 / 8 / 14 / 2 |
| Z-C / PSM | 4 / 18 |
| LED | 13 |
| PAIR / CONFIG | 12 / 16 |

Przyciski skonfigurowałem jako aktywne stanem niskim z wewnętrznym podciąganiem wejść (`INPUT_PULLUP`). Referencyjny build odpowiada profilowi `ESP32S3 Dev Module` i zakłada 8 MB pamięci flash, tryb DIO 80 MHz, wyłączoną pamięć PSRAM, interfejs USB Hardware CDC/JTAG, wyłączoną opcję CDC on boot oraz schemat partycji `default_8MB`. W przyjętej przeze mnie mapie wyprowadzeń sygnały omijają linie GPIO19/20 (zarezerwowane dla USB), piny pamięci SPI oraz piny strappingowe ESP32-S3. Przed wgraniem oprogramowania na fizyczną płytkę weryfikuję jej dokładne oznaczenie, okablowanie oraz ewentualne funkcje dodatkowe na pinach. Wszystkie linie GPIO pracują w logice 3,3 V. Przed podłączeniem modułu wykonawczego sprawdzam poziom napięcia, polaryzację i izolację galwaniczną sygnału Z-C oraz wymagania prądowo-napięciowe wejścia PSM. Po stronie sprzętowej należy też zapewnić bezpieczny stan spoczynkowy wejścia PSM podczas resetu i uruchamiania mikrokontrolera ESP32. Dokumentacja referencyjna: [ESP32-S3-DevKitC-1](https://documentation.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide.html), [nota katalogowa ESP32-S3](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf), [RobotDyn RBDDimmer](https://github.com/RobotDynOfficial/RBDDimmer).

## Profile

- Profil `dry-run` wystawia wyłącznie symulowane wyjście, ale przyjmuje rzeczywiste notyfikacje HR z pasa BLE. W testach hostowych dostarczam sztuczne przebiegi HR oraz Z-C bez udziału sprzętu.
- Profil `ac-hardware` korzysta z fizycznego wejścia Z-C oraz sprzętowego timera, jednak domyślna struktura profilu w pliku `Board.h` jest celowo pusta. Parametry `min`/`max`, opcjonalnego rozruchu, wybranego zbocza, przesunięcia czasowego i szerokości impulsu muszę wyznaczyć podczas pomiarów konkretnego modułu oraz silnika. Po ich wpisaniu profil wymaga ustawienia flagi `approved=true`, pomyślnej walidacji oraz świadomego wyboru trybu AUTO lub MANUAL po starcie. Pusty lub niepoprawny profil utrzymuje wyjście PSM w stanie niskim.

Zależność pomiędzy nastawą a opóźnieniem wyzwalania (`nastawa -> opóźnienie`) jest liniowa wewnątrz zatwierdzonego zakresu roboczego. Nastawa minimalna odpowiada wartości `latestUs`, a maksymalna wartości `earliestUs`. Jeśli oba krańce zakresu są równe, jedyna dostępna nastawa korzysta z opóźnienia `latestUs`. Początek fazy liczę od zaakceptowanego zbocza Z-C. Obowiązują następujące ograniczenia implementacyjne w ramach budżetu półokresu sieci 50 Hz: `earliestUs >= 250`, `latestUs > earliestUs`, szerokość impulsu `gateUs` w przedziale `40-500 us` oraz suma `latestUs + gateUs <= 8600 us`. Rzeczywistą charakterystykę modułu, przesunięcie detekcji zera oraz zachowanie samego napędu wyznaczam pomiarowo.

## H01 - sterownik bez zasilania części AC

Szczegółowy arkusz wykonawczy z oczekiwanymi wynikami i tabelą pomiarów `diag` znajduje się w pliku [H01_PROTOKOL.md](H01_PROTOKOL.md).

1. Potwierdzam dokładny wariant płytki, rozmiar pamięci flash, działanie portu USB, mapę GPIO oraz konfigurację ekranu. Zapisuję identyfikator wgranego obrazu.
2. W profilu `dry-run` sprawdzam start w trybie OFF, reakcję na oba przyciski, wskazania ekranu, blokadę przy pustym NVS oraz zachowanie zapisanej konfiguracji po restarcie.
3. Testuję skanowanie przy kilku pasach BLE w zasięgu, wybór pasa z listy, obsługę utraty kontaktu ze skórą, rozłączenie, procedurę reconnect, timeout 5 s oraz wymóg odebrania dwóch świeżych próbek. W trybie MANUAL odpinam pas w trakcie pracy, a w trybie OFF weryfikuję samoczynny reconnect w tle.
4. Na komputerze z Windows sprawdzam parowanie kodem PIN, odrzucenie nieautoryzowanego klienta, próbę parowania z błędnym kodem PIN, kasowanie parowań przyciskiem, wygaśnięcie okna CONFIG, zamknięcie sesji komendą `CLOSE`, wznowienie szyfrowanego połączenia oraz odczyt kontrolny `GET` po `SAVE`. Jeśli stanowisko na to pozwala, powtarzam próbę przy wymuszonym rozmiarze ATT MTU równym 23.
5. Odczytuję wolną stertę RAM oraz zapas stosów zadań podczas skanowania, wznawiania połączenia i zapisu konfiguracji. Na urządzeniu testowym sprawdzam też zachowanie przy celowym zerwaniu połączenia oraz restarcie zasilania w trakcie operacji `SAVE`.

## H02 - Z-C/PSM na stanowisku pomiarowym

1. Na bezpiecznych sygnałach testowych potwierdzam okres 10 ms pomiędzy kolejnymi właściwymi zboczami Z-C, przesunięcie fazowe oraz polaryzację sygnału. Przy pustym profilu AC potwierdzam całkowity brak impulsów na wyjściu PSM.
2. Po wgraniu profilu pomiarowego (nadal bez podłączonego silnika) sprawdzam na oscyloskopie nastawy skrajne i pośrednie, szerokość impulsu bramkującego, margines czasu przed końcem półokresu oraz brak jakichkolwiek impulsów podczas startu, resetu i w trybie OFF. Wyznaczam wpływ kształtu sygnału Z-C na rzeczywisty moment wyzwalania.
3. Wstrzykuję zakłócenia: zbyt bliskie zbocza, brakujące półokresy oraz celowe zatrzymanie zadania sterowania przy działających przerwaniach ISR. Wyzwalając fizyczne OFF tuż przed zaplanowanym alarmem oraz w trakcie trwania impulsu, mierzę czas do opadnięcia linii PSM i weryfikuję, że po usterce fazowej układ nie wznawia pracy bez pełnego resetu i świadomego wyboru trybu.
4. Powtarzam pomiary przy jednoczesnej komunikacji BLE i odświeżaniu ekranu. Potwierdzam, że wyjście pozostaje wyłączone w trybie CONFIG oraz po wymuszeniu błędów inicjalizacji. Docelowy czas od stabilnego naciśnięcia przycisku OFF do wygaszenia linii PSM wynosi maksymalnie 35 ms (kryterium weryfikowane oscyloskopem). Zebrane rozkłady opóźnień i zakłóceń Z-C posłużą do decyzji o ewentualnym wdrożeniu stopniowania usterek opisanego w [PROJEKT_USTEREK_FAZOWYCH.md](PROJEKT_USTEREK_FAZOWYCH.md).

## H03 - odbiór napędu

Identyfikuję dokładny model i typ silnika wentylatora oraz potwierdzam, że producent dopuszcza jego regulację fazową. Mierzę skuteczność rozruchu na zimno i na ciepło, stabilność pracy przy nastawie minimalnej i na krańcach zakresu, pobór prądu oraz temperaturę uzwojeń w długim cyklu pracy. Na tej podstawie ustalam bezpieczne parametry zatwierdzonego profilu sprzętowego. Sprawdzam zachowanie układu po utracie sygnału HR oraz po zaniku zasilania w trakcie treningu, dokumentując wyniki, użyte przyrządy pomiarowe i identyfikator obrazu firmware'u.

Programowe przejście w tryb OFF natychmiast wygasza generowanie impulsów bramkujących, jednak sam triak w stopniu wykonawczym przewodzi prąd do momentu naturalnego spadku poniżej prądu podtrzymania przy kolejnym przejściu przez zero. Zwarcie struktury triaka, awaria mikrokontrolera, nieoczekiwany reset lub uszkodzenie okablowania wymagają zastosowania niezależnego zabezpieczenia sprzętowego (np. bezpiecznika topikowego i wyłącznika fizycznego). Układ w obecnej architekturze nie posiada dodatkowych czujników i nie mierzy prędkości obrotowej ani temperatury silnika.
