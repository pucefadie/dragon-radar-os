# Protocollo radio e alimentazione

La radio usa ESP-NOW in modalità STA. Il frame locale DRB2 è serializzato campo per campo in little-endian, lunghezza fissa 34 byte e CRC16-CCITT. Tipi già enumerati: beacon, discovery, pairing, heartbeat, status, ping/pong, command/ACK, found, reset e batteria. Al momento il trasporto applicativo si limita ai beacon non autenticati; l'enumerazione non implica che le altre transazioni siano operative.

Il CRC rileva errori casuali, non autentica. Il MAC non è una credenziale. Non attivare motori, buzzer o altri attuatori sulla base di pacchetti broadcast. Prima dell'uso reale completare pairing esplicito, associazione persistente ID↔MAC, provisioning LMK e cifratura unicast; verificare i limiti peer della versione ESP-IDF/Arduino effettivamente installata.

Il radar è ID 0, sfere 1–7 e broadcast 255 nel protocollo pianificato. S3 e C6 devono condividere il canale; il Wi-Fi associato a un AP può cambiare il canale radio. La sfera seleziona antenna esterna prima di inizializzare Wi-Fi (GPIO3 LOW, GPIO14 HIGH); questa configurazione va usata soltanto col cavo U.FL collegato.

Il motore, buzzer, LED, ADC batteria, sensore di scuotimento e deep sleep della sfera non sono implementati/verificati. Nessun consumo o autonomia è stato misurato. Lasciare gli attuatori disabilitati finché driver e alimentazione non sono verificati; non pilotare carichi direttamente dai GPIO.
