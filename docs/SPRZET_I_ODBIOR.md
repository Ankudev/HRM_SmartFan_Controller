# Sprzęt i odbiór

ESP32-S3-WROOM-1, GC9A01 240 × 240 SPI, pas Garmin HRM-Pro Plus lub inne zgodne HRS, regulator RobotDyn Z-C/PSM i wentylator użytkownika 220–240 V, około 60 W, 50 Hz.

| Sygnał | GPIO |
|---|---:|
| TFT SCK / MOSI | 10 / 11 |
| TFT CS / DC / RESET / BL | 9 / 8 / 14 / 2 |
| Z-C / PSM | 4 / 18 |
| LED | 13 |
| PAIR / CONFIG | 12 / 16 |

Przyciski są aktywne stanem niskim z podciąganiem wejść. Referencyjny build odpowiada ogólnemu `ESP32S3 Dev Module` i zakłada 8 MB flash, DIO 80 MHz, bez PSRAM, USB Hardware CDC/JTAG, CDC on boot wyłączone, partycje `default_8MB`. W tej mapie sygnały omijają GPIO19/20 dla USB, piny pamięci SPI i piny strap ESP32-S3. Zweryfikuj oznaczenie rzeczywistej płytki, okablowanie i funkcje dodatkowe przed wgraniem. Traktuj GPIO jako sygnały 3,3 V. Sprawdź poziom, polaryzację i izolację Z-C oraz wymagania wejścia PSM konkretnego modułu. Zapewnij sprzętowy stan spoczynkowy wejścia PSM podczas resetu i startu ESP32. [ESP32-S3-DevKitC-1](https://documentation.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide.html), [dokumentacja ESP32-S3](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf), [RobotDyn RBDDimmer](https://github.com/RobotDynOfficial/RBDDimmer).

## Profile

- `dry-run` daje symulowane wyjście. Przyjmuje realne notyfikacje HR. Testy hostowe dostarczają sztuczne HR i Z-C bez sprzętu.
- `ac-hardware` używa rzeczywistego Z-C i sprzętowego timera, a obecny profil w `Board.h` jest pusty. Parametry min/max, opcjonalnego rozruchu, wybranego zbocza, przesunięcia i impulsu muszą wynikać z odbioru modułu oraz napędu. Po ich wpisaniu profil wymaga `approved=true`, walidacji i świadomego wyboru AUTO lub MANUAL po starcie. Pusty lub niepoprawny profil utrzymuje PSM nisko.

Zależność nastawa→opóźnienie jest liniowa wewnątrz zatwierdzonego zakresu. Minimum odpowiada `latestUs`, maksimum `earliestUs`. Jeśli oba krańce nastawy są równe, jedyna nastawa używa `latestUs`. Początek fazy jest liczony od zaakceptowanego zbocza. `earliestUs ≥ 250`, `latestUs > earliestUs`, szerokość impulsu 40–500 µs, suma `latestUs + gateUs ≤ 8600 µs`. Są to ograniczenia implementacji i budżet półokresu. Charakterystyka modułu, przesunięcie detekcji i zachowanie napędu wymagają pomiaru.

## H01 — sterownik bez zasilania części AC

1. Potwierdzić wariant płytki, flash, USB, mapę GPIO i ustawienia ekranu. Zarejestrować identyfikator obrazu.
2. W `dry-run` sprawdzić boot OFF, oba przyciski, stan ekranu, konfigurację od pustego NVS i zapis po restarcie.
3. Sprawdzić skan przy kilku pasach, wybór, brak kontaktu, rozłączenie, reconnect, timeout 5 s i dwie próbki. W MANUAL odpiąć pas, a w OFF odtworzyć reconnect.
4. W Windows sprawdzić parowanie PIN, klienta obcego, próbę z błędnym PIN, usunięcie parowań, wygaśnięcie okna, rozłączenie CLOSE, ponowne szyfrowanie linku i GET po SAVE. Powtórzyć przy ATT MTU 23, o ile stanowisko pozwala go wymusić.
5. Odczytać wolną pamięć i zapas stosu przy skanie, reconnect oraz konfiguracji. Sprawdzić zerwanie linku i restart podczas SAVE na urządzeniu testowym.

## H02 — Z-C/PSM na stanowisku pomiarowym

1. Na sygnałach testowych potwierdzić czas 10 ms pomiędzy właściwymi zboczami Z-C, przesunięcie i polaryzację. Z pustym profilem AC potwierdzić brak impulsów.
2. Z profilem pomiarowym i bez podłączonego silnika sprawdzić oscyloskopem nastawy krańcowe i pośrednie, szerokość impulsu, odstęp od końca półokresu oraz brak impulsów podczas boot/reset i OFF. Ustalić wpływ Z-C na rzeczywisty punkt wyzwalania.
3. Dodać zbyt bliskie zbocza, brakujące półokresy i zatrzymanie zadania sterowania przy działających ISR. Wyzwolić OFF przed zaplanowanym alarmem i podczas impulsu. Zmierzyć czas do ostatniego PSM oraz sprawdzić brak wznowienia po usterce bez resetu i świadomego wyboru.
4. Powtórzyć przy pracy BLE i odświeżaniu ekranu. Sprawdzić, czy sterownik pozostaje OFF podczas CONFIG i przy błędach inicjalizacji. Docelowy czas zadziałania fizycznego OFF do PSM to maks. 35 ms od stabilnego naciśnięcia. Jest to kryterium do pomiaru.

## H03 — odbiór napędu

Zidentyfikować model i rodzaj silnika oraz potwierdzić dopuszczalność regulacji fazowej. Zmierzyć skuteczność rozruchu na zimno i ciepło, pracę przy minimum i krańcach zakresu, prąd oraz temperaturę w długim cyklu. Ustalić warunki i parametry zatwierdzenia profilu. Sprawdzić utratę HR i zasilania w trakcie treningu. Udokumentować wyniki, urządzenia pomiarowe i wersję obrazu.

Programowe OFF wygasza wyzwalanie. Triak może przewodzić do chwili spadku prądu poniżej prądu podtrzymania. Zwarcie triaka, awaria procesora, reset lub uszkodzenie okablowania mogą wymagać niezależnego zabezpieczenia sprzętowego. Układ bez dodatkowych czujników nie mierzy obrotów ani temperatury silnika.
