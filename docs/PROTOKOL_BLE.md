# Protokół BLE v2

Sterownik przełącza się w rolę urządzenia peryferyjnego wyłącznie na czas fizycznie otwartego okna CONFIG. Okno to trwa 180 s od momentu wejścia, wliczając czas oczekiwania na zamknięcie połączenia z pasem HR. Tryb CONFIG całkowicie blokuje napęd. Pakiet rozgłoszeniowy BLE zawiera identyfikator usługi, a odpowiedź na skan zwraca nazwę **SmartFan v2**.

| Element | UUID | Właściwości |
|---|---|---|
| Usługa | `578c0001-a943-42e2-90d7-e9eb82a2a89b` | GATT |
| RX | `578c0002-a943-42e2-90d7-e9eb82a2a89b` | Write Request, wymagane szyfrowanie i uwierzytelnienie |
| TX | `578c0003-a943-42e2-90d7-e9eb82a2a89b` | Notify, wysyłka wyłącznie do aktualnego uwierzytelnionego klienta |

W polityce bezpieczeństwa NimBLE wymusiłem bonding oraz ochronę MITM, deklarując obsługę LE Secure Connections i rolę `DisplayOnly`. Przy każdym nowym żądaniu PIN generuję sześć losowych cyfr za pomocą `esp_random` i wyświetlam je lokalnie na ekranie w trakcie parowania. W systemie Windows parowanie przeprowadzam przez systemowe ustawienia Bluetooth, ponieważ w przypiętej wersji biblioteki Bleak 2.1.1 procedura `pair()` oparta na WinRT obsługuje jedynie tryb `ConfirmOnly`, a wpisanie kodu PIN z ekranu realizuje sam system Windows. Po stronie klienta ustawiam poziom ochrony charakterystyki RX na `EncryptionAndAuthenticationRequired`. Niezależnie od tego w oprogramowaniu układowym sprawdzam stan szyfrowania, uwierzytelnienia, bondingu oraz token bieżącego połączenia. Przed wysłaniem żądania klient musi aktywować subskrypcję TX. Fizyczne przytrzymanie przycisku CONFIG w trybie CONFIG kończy okno konfiguracji, zamyka połączenie i kasuje zapisane parowania. Po tej operacji usuwam **SmartFan v2** także z listy sparowanych urządzeń w ustawieniach Windows. Dokumentacja referencyjna: [NimBLE-Arduino 2.3.7](https://github.com/h2zero/NimBLE-Arduino/tree/2.3.7), [WinRT GattCharacteristic.ProtectionLevel](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattcharacteristic.protectionlevel).

## Ramka

Wszystkie pola liczbowe zapisuję w porządku little endian. Nagłówek ramki ma stałą długość 10 bajtów.

| Offset | Rozmiar | Pole |
|---:|---:|---|
| 0 | 2 | ASCII `SF` |
| 2 | 1 | Wersja 2 |
| 3 | 1 | Opcode, odpowiedź ma ustawiony bit 7 |
| 4 | 4 | Rosnące ID transakcji w ramach jednego połączenia, od 1 |
| 8 | 2 | Rozmiar danych, 0-31 |
| 10 | zmienny | Dane |
| 10 + rozmiar | 4 | CRC-32/ISO-HDLC nagłówka i danych |

Klient dzieli wiadomości na fragmenty do 20 bajtów i korzysta z zapisu z potwierdzeniem ATT. Sterownik potrafi przetworzyć strumień zawierający wiele ramek w jednym zapisie o długości do 90 bajtów. Potwierdzenie zapisu ATT dotyczy wyłącznie transportu fragmentu, natomiast wynik operacji aplikacyjnej wraca jako osobna odpowiedź ze zgodnym identyfikatorem transakcji. Przerwa trwająca ponad 2 s między fragmentami unieważnia niepełną ramkę. Błędna suma CRC albo długość danych spoza dozwolonego zakresu powodują odrzucenie ramki, a przepełnienie kolejki RX zamyka połączenie i usuwa buforowanego kandydata `SET`.

| Kod | Żądanie | Dane sukcesu po bajcie statusu |
|---:|---|---|
| 1 GET | Puste | Pełna, zapisana konfiguracja |
| 2 SET | Pełny kandydat | Puste. Kandydat jest przechowywany w RAM bieżącej sesji |
| 3 SAVE | Puste | Pełna konfiguracja po zapisie i odczycie NVS |
| 4 CLOSE | Puste | Puste. Sterownik kończy sesję, potem rozłącza link |

Po odebraniu komendy `CLOSE` sterownik odczekuje 750 ms na dostarczenie odpowiedzi, po czym inicjuje zamknięcie połączenia. Klient czeka na odpowiedź oraz systemowe zdarzenie rozłączenia, zanim rozpocznie kolejny odczyt w nowej sesji. Za potwierdzenie (ACK) uznaję poprawny kod odpowiedzi, zgodny numer transakcji, kompletną ramkę oraz prawidłowe CRC. Po 3 s bez odpowiedzi klient może ponowić ostatnią kompletną wiadomość z tym samym identyfikatorem transakcji (maksymalnie do trzech prób wysyłki). Serwer zapamiętuje ostatnie żądanie i wygenerowaną odpowiedź, dzięki czemu identyczne ponowienie odtwarza odpowiedź bez ponownego wykonywania zapisu `SAVE` do pamięci flash. Starsze identyfikatory transakcji oraz wiadomości z tym samym ID, ale inną treścią, są odrzucane. Po wygaśnięciu okna CONFIG lub zerwaniu połączenia niezapisany kandydat konfiguracji przepada.

Statusy odpowiedzi: `0` OK, `1` zły format lub wersja, `2` błędna konfiguracja, `3` brak kandydata, `4` błąd zapisu lub odczytu kontrolnego, `5` błąd sekwencji, `6` brak ważnego rekordu. Odpowiedzi błędów zawierają wyłącznie bajt statusu. Suma CRC odpowiada za kontrolę integralności ramki, natomiast szyfrowanie i uwierzytelnienie realizuje warstwa bezpieczeństwa BLE.

Struktura konfiguracji obejmuje kolejno 15 wartości `uint16`: pięć progów BPM, pięć odpowiadających im nastaw, histerezę BPM, stałą czasową filtru w ms, tempo wzrostu i spadku w punktach nastawy na sekundę oraz domyślną nastawę ręczną. Wersja schematu danych wynika bezpośrednio z wersji ramki. Plik JSON używany przy eksporcie i imporcie przechowuje te same pola wraz z numerem wersji. Profil elektryczny napędu oraz klucze parowania BLE znajdują się poza tym schematem.

## Trwałość

Konfigurację przechowuję w pamięci NVS jako pojedynczy rekord binarny (blob) z wersją i sumą CRC. Wywołanie `Preferences::putBytes` realizuje pod spodem operacje `nvs_set_blob` oraz `nvs_commit`. Po każdym zapisie sterownik weryfikuje długość i odczytuje cały rekord kontrolnie. Dopiero po pomyślnym odczycie zadanie sterowania otrzymuje nową konfigurację, a do klienta trafia potwierdzenie `SAVE`. Uszkodzony lub nieprawidłowy rekord blokuje wyjście sterownika przy kolejnym uruchomieniu. W razie zaniku zasilania w trakcie aktualizacji pamięć NVS zachowuje poprzednią wersję lub udostępnia nowy, kompletny rekord. Uszkodzenie nośnika wykraczające poza gwarancje mechanizmu NVS skutkuje brakiem ważnego rekordu i bezpieczną blokadą wyjścia. Jeśli utracone zostanie samo potwierdzenie ACK po udanym zapisie, w sterowniku pozostaje już nowa konfiguracja. Szczegóły działania pamięci nieulotnej opisuje dokumentacja [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html).
