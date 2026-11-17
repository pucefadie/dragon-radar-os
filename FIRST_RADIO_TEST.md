# Primo collaudo radio: preparazione

**PENDING:** questa procedura fisica non è ancora stata eseguita. La logica DEV ha superato una simulazione host; l’adapter radio e la comunicazione fisica restano da verificare. Il firmware DEV è in chiaro e non autenticato. Nessun attuatore deve essere collegato o comandato. Pairing e cifratura non sono implementati.

1. Collegare l'antenna esterna U.FL allo XIAO prima di alimentarlo; alimentare entrambe le schede via USB. Identificare distintamente le porte seriali.
2. Con il radar acceso, aprire il suo monitor a 115200 baud e inviare `beacons`. Annotare `channel=`: con Wi-Fi associato il canale è imposto dall'AP. Il radar non cambia il canale per questa prova.
3. Compilare entrambi i firmware DEV sostituendo `4` con il canale effettivo del radar: `RADIO_DEV_INSECURE=1 ./scripts/build.sh full` e `RADIO_DEV_INSECURE=1 ./scripts/build_beacon.sh 1 4 esp32:esp32:XIAO_ESP32C6`. Le build scrivono rispettivamente in `build/out/` e `build/beacon-1/`; le copie DEV verificate di questa revisione si trovano in `build/dev-tx/s3.bin` e `build/dev-tx/c6.bin`.
4. Prima del flash identificare target, porta e partizioni: la procedura di upload dipende dallo stato reale delle due schede e non è stata verificata per questo snapshot. Non usare erase flash. Nessuno script di flash è incluso qui.
5. In due terminali WSL separati eseguire i comandi qui sotto, sostituendo le porte con quelle effettivamente identificate. Inviare `beacons` nel monitor del radar. Uscire da ciascun monitor con Ctrl+C.

   ```bash
   "/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" monitor -p COM_S3 -c baudrate=115200
   "/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" monitor -p COM_C6 -c baudrate=115200
   ```

6. Attesi: `[DEV INSECURE]`, BEACON sul radar, `DISCOVERY`, `STATUS`, `PING`, `PONG`, `COMMAND`, `COMMAND_ACK`, `HEARTBEAT` e `LINK_RECOVERED`. `queued=1` significa ingresso nella coda locale; `TX driver=OK` significa esito MAC del driver; `RESPONSE` indica risposta applicativa correlata e riporta RTT. I messaggi RX sono ricevuti ma non necessariamente validati. In caso di timeout compaiono `TIMEOUT`, `TX_BUSY_TIMEOUT` oppure `LINK_LOST`. La sfera richiede una nuova discovery dopo 10 secondi senza richieste e mantiene il primo MAC fino al riavvio. Il prototipo DEV supporta soltanto la sfera ID 1.


Se non compare alcun beacon, controllare alimentazione, antenna e canali effettivi. Nessun beacon può significare anche altri problemi: non dimostra un disallineamento di canale. Il C6 attualmente non ricerca altri canali. Se il radar vede il beacon ma non riceve risposte, controllare che entrambi i binari siano stati compilati con `RADIO_DEV_INSECURE=1`, che i log indichino tale modalità e che entrambi usino lo stesso canale.

Se compare `TX callback timeout; radio TX suspended`, non vengono avviati altri invii finché non arriva il completamento; se non arriva, riavviare la scheda e conservare il log. La modalità DEV non autentica né cifra i pacchetti.
