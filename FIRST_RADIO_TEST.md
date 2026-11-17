# Primo collaudo radio: preparazione

**PENDING:** questa procedura non è ancora stata eseguita e il trasporto DEV non ha superato test end-to-end simulati. Il firmware DEV è in chiaro e non autenticato. Nessun attuatore deve essere collegato o comandato. Pairing e cifratura non sono implementati.

1. Collegare l'antenna esterna U.FL allo XIAO prima di alimentarlo; alimentare entrambe le schede via USB. Identificare distintamente le porte seriali.
2. Con il radar acceso, aprire il suo monitor a 115200 baud e inviare `beacons`. Annotare `channel=`: con Wi-Fi associato il canale è imposto dall'AP. Il radar non cambia il canale per questa prova.
3. Compilare entrambi i firmware DEV sostituendo `4` con il canale effettivo del radar: `RADIO_DEV_INSECURE=1 ./scripts/build.sh full` e `RADIO_DEV_INSECURE=1 ./scripts/build_beacon.sh 1 4 esp32:esp32:XIAO_ESP32C6`. I binari si trovano rispettivamente in `build/out/` e `build/beacon-1/`.
4. Prima del flash identificare target, porta e partizioni: la procedura di upload dipende dallo stato reale delle due schede e non è stata verificata per questo snapshot. Non usare erase flash. Nessuno script di flash è incluso qui.
5. Aprire due monitor seriali a 115200 baud. Sul C6 aspettarsi `Dragon beacon ID=1 channel=...` e `[DEV INSECURE] controller online`; sul radar `[DEV INSECURE] enqueue` e `response=... RTT=...`. Il valore `enqueued=0` indica accettazione locale, non consegna radio. `response=6`, `8` e `10` indicano rispettivamente STATUS, PONG e COMMAND_ACK.
6. Dopo perdita di comunicazione il C6 segnala `controller timeout; discovery required`. Il radar segnala timeout o link lost. Una nuova discovery dallo stesso MAC riapre il collegamento. Il C6 mantiene il primo MAC fino a riavvio; riavviarlo per provare un altro radar.

Se non compare alcun beacon, controllare alimentazione, antenna e canali effettivi. Nessun beacon può significare anche altri problemi: non dimostra un disallineamento di canale. Il C6 attualmente non ricerca altri canali. Se il radar vede il beacon ma non riceve risposte, controllare che entrambi i binari siano stati compilati con `RADIO_DEV_INSECURE=1`, che i log indichino tale modalità e che entrambi usino lo stesso canale.
