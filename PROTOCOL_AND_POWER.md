# Protocollo radio e alimentazione

DRB2 mantiene il frame broadcast da 34 byte, little-endian e CRC16. Nei frame diretti, il flag DIRECTED riusa il byte reserved come payload length e il precedente payload-length byte come destinazione; i beacon broadcast restano compatibili. ID radar 0, sfere 1–7, broadcast 255. Il codec verifica tipo, ID, flags, dimensione e CRC.

Sono implementati e testati su host `PairingRegistry`, `ReplayWindow` e `RadioAckTracker`. Sono macchine pure in RAM: non sono ancora collegate al trasporto ESP-NOW o a NVS. La registry contiene chiavi soltanto in RAM e non è un provisioning sicuro. Non c'è ancora cifratura ESP-NOW o pairing effettivo; CRC e MAC non autenticano. Il service S3 continua a ricevere beacon. Il C6 trasmette beacon, senza RX applicativo.

I beacon C6 sono schedulati circa ogni 500 ms con jitter ±30 ms. Il canale è fissato in build; il C6 non segue automaticamente il canale AP del radar. Configurare lo stesso canale legale su entrambi e verificare quello effettivo con `beacons`. Con un AP che impone un altro canale, il link C6 non è disponibile finché la configurazione non viene allineata. Non è ancora implementata la selezione/recovery automatica del canale.

Mancano adapter TX/RX bidirezionale, discovery/pairing espliciti, persistenza ID↔MAC, provisioning LMK protetto, cifratura ESP-NOW unicast, ACK/retry nel task radio, heartbeat/PING-PONG e perdita link. Il limite ESP-IDF dei peer cifrati per sette sfere va verificato sulla versione installata prima di dichiarare supporto multi-peer sicuro.

L'antenna esterna XIAO viene selezionata prima di avviare la radio: GPIO3 LOW, GPIO14 HIGH. Collegare l'antenna U.FL. Attuatori, ADC batteria, shake sensor e deep sleep non sono implementati; non pilotare carichi direttamente dai GPIO. Non è stata misurata autonomia.
