# Protokół BLE v2

Sterownik jest urządzeniem peryferyjnym tylko na potrzeby fizycznie otwartego okna CONFIG. Okno trwa 180 s od wejścia, także podczas oczekiwania na zamknięcie połączenia HR. CONFIG blokuje napęd. Reklama BLE zawiera identyfikator usługi, a odpowiedź na skan nazwę **SmartFan v2**.

| Element | UUID | Właściwości |
|---|---|---|
| Usługa | `578c0001-a943-42e2-90d7-e9eb82a2a89b` | GATT |
| RX | `578c0002-a943-42e2-90d7-e9eb82a2a89b` | Write Request, wymagane szyfrowanie i uwierzytelnienie |
| TX | `578c0003-a943-42e2-90d7-e9eb82a2a89b` | Notify, wysyłka wyłącznie do aktualnego uwierzytelnionego klienta |

Polityka NimBLE wymaga bondingu i MITM, deklaruje obsługę LE Secure Connections i rolę DisplayOnly. Każde nowe żądanie PIN generuje sześć cyfr z `esp_random`. PIN jest wyświetlany lokalnie w trakcie parowania. W Windows użyj ustawień systemowych Bluetooth. W przypiętym Bleak 2.1.1 procedura `pair()` WinRT obsługuje ConfirmOnly, a podanie kodu z ekranu należy do ustawień Windows. Klient ustawia poziom ochrony RX na EncryptionAndAuthenticationRequired. Firmware sprawdza także stan szyfrowania, uwierzytelnienia, bondingu i token bieżącego połączenia. Klient musi mieć subskrypcję TX przed wysłaniem żądania. Fizyczne przytrzymanie CONFIG w CONFIG kończy okno, zamyka połączenie i usuwa zapisane parowania. Usuń SmartFan także z listy par w Windows. [NimBLE-Arduino 2.3.7](https://github.com/h2zero/NimBLE-Arduino/tree/2.3.7), [WinRT GattCharacteristic.ProtectionLevel](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattcharacteristic.protectionlevel).

## Ramka

Kolejność liczb little endian. Nagłówek ma 10 bajtów.

| Offset | Rozmiar | Pole |
|---:|---:|---|
| 0 | 2 | ASCII `SF` |
| 2 | 1 | Wersja 2 |
| 3 | 1 | Opcode, odpowiedź ma ustawiony bit 7 |
| 4 | 4 | Rosnące ID transakcji w ramach jednego połączenia, od 1 |
| 8 | 2 | Rozmiar danych, 0–31 |
| 10 | zmienny | Dane |
| 10 + rozmiar | 4 | CRC-32/ISO-HDLC nagłówka i danych |

Klient dzieli wiadomości na fragmenty do 20 bajtów i używa zapisu z odpowiedzią ATT. Sterownik potrafi przetworzyć strumień z wieloma ramkami w jednym zapisie do 90 bajtów. Zapis ATT potwierdza transport fragmentu. Wynik operacji jest osobną odpowiedzią z dopasowanym ID. Dwie sekundy bez fragmentu unieważniają niepełną ramkę. Złe CRC albo długość poza zakresem powodują odrzucenie ramki. Przepełnienie kolejki RX zamyka połączenie i usuwa kandydata SET.

| Kod | Żądanie | Dane sukcesu po bajcie statusu |
|---:|---|---|
| 1 GET | Puste | Pełna, zapisana konfiguracja |
| 2 SET | Pełny kandydat | Puste. Kandydat jest przechowywany w RAM bieżącej sesji |
| 3 SAVE | Puste | Pełna konfiguracja po zapisie i odczycie NVS |
| 4 CLOSE | Puste | Puste. Sterownik kończy sesję, potem rozłącza link |

Po CLOSE sterownik daje 750 ms na dostarczenie odpowiedzi i żąda zamknięcia połączenia. Klient czeka na odpowiedź oraz zdarzenie rozłączenia od systemu, zanim zacznie nowy odczyt. ACK transmisji to kod odpowiedzi, numer transakcji, kompletna ramka i prawidłowe CRC. Po 3 s bez odpowiedzi klient może ponowić ostatnią kompletną wiadomość z tym samym ID, maksymalnie do trzech wysyłek. Serwer pamięta ostatnie żądanie i odpowiedź. Identyczne ponowienie odtwarza odpowiedź bez ponownego SAVE. Starsze ID i te same ID z inną treścią są odrzucane. Po wygaśnięciu okna i po zerwaniu linku niezapisany kandydat przepada.

Statusy odpowiedzi: 0 OK, 1 zły format lub wersja, 2 błędna konfiguracja, 3 brak kandydata, 4 błąd zapisu lub odczytu kontrolnego, 5 błąd sekwencji, 6 brak ważnego rekordu. Błędy zawierają tylko status. CRC zapewnia kontrolę kompletności, a szyfrowanie i uwierzytelnienie realizuje standard BLE.

Konfiguracja zawiera kolejno 15 wartości `uint16`: pięć progów BPM, pięć odpowiadających nastaw, histereza BPM, stała filtru w ms, wzrost i spadek w punktach nastawy na sekundę, domyślna nastawa ręczna. Wersja schematu wynika z wersji ramki. JSON eksportu zawiera tę samą konfigurację oraz wersję. Profil elektryczny napędu i informacje o kluczach BLE pozostają poza tym schematem.

## Trwałość

NVS przechowuje konfigurację jako jeden blob z wersją i CRC. `Preferences::putBytes` wykonuje `nvs_set_blob` i `nvs_commit`. Sterownik sprawdza długość i odczytuje cały rekord po zapisie. Dopiero wtedy sterowanie otrzymuje pełną nową konfigurację i może zostać wysłane potwierdzenie. Nieprawidłowy rekord blokuje wyjście przy następnym starcie. Przy zaniku zasilania w trakcie aktualizacji oczekiwane zachowanie NVS to zachowanie poprzedniego lub odczyt nowego kompletnego rekordu. Stan nośnika wykraczający poza gwarancje NVS może skutkować brakiem ważnego rekordu. Utrata ACK po udanym zapisie może pozostawić nową konfigurację. [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html).
